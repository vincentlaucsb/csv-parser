#pragma once

#include "../parser/driver.hpp"
#include "diagnostics.hpp"

#include <cmath>
#include <limits>

#if CSV_ENABLE_THREADS

namespace csv {
    namespace internals {
        namespace speculative {
        constexpr size_t CSV_SPECULATIVE_PREFIX_SIZE = 64 * 1024;

        struct PrefixScanResult {
            ParserDFAState ending_state;
            size_t records_seen = 0;
            size_t first_record_end = (std::numeric_limits<size_t>::max)();
            size_t total_states = 0;
            size_t unquoted_states = 0;
            size_t max_record_length = 0;
            size_t quoted_fields = 0;
            size_t first_quote_open = (std::numeric_limits<size_t>::max)();
            size_t first_quote_close = (std::numeric_limits<size_t>::max)();
            long double log_other_start_valid_probability = 0;
        };

        struct ChunkSpeculation {
            size_t sequence_number = 0;
            size_t offset = 0;
            size_t length = 0;
            size_t prefix_length = 0;
            ParserDFAState assumed_start_state;
            PrefixScanResult outside_scan;
            PrefixScanResult inside_scan;
            bool ambiguous = false;
            bool used_probability_model = false;
            bool used_record_size_heuristic = false;
            long double quoted_start_odds = 0;
        };

        inline void observe_speculation(
            SpeculativeParseDiagnostics& diagnostics,
            const ChunkSpeculation& speculation
        ) noexcept {
            diagnostics.chunks++;
            if (speculation.ambiguous) {
                diagnostics.ambiguous_chunks++;
            }
            if (speculation.used_probability_model) {
                diagnostics.probability_model_chunks++;
            }
            if (speculation.used_record_size_heuristic) {
                diagnostics.record_size_heuristic_chunks++;
            }
            if (speculation.assumed_start_state.quote_escape) {
                diagnostics.assumed_quoted_chunks++;
            }
            else {
                diagnostics.assumed_unquoted_chunks++;
            }
        }

        class SpeculativeScanner {
        public:
            explicit SpeculativeScanner(
                const ParseFlagMap& parse_flags,
                size_t prefix_bytes = CSV_SPECULATIVE_PREFIX_SIZE
            ) : parse_flags_(parse_flags), prefix_bytes_(prefix_bytes) {}

            ChunkSpeculation speculate(
                size_t sequence_number,
                size_t offset,
                csv::string_view chunk
            ) const {
                const size_t prefix_length = std::min(chunk.size(), this->prefix_bytes_);
                const csv::string_view prefix(chunk.data(), prefix_length);

                ChunkSpeculation result;
                result.sequence_number = sequence_number;
                result.offset = offset;
                result.length = chunk.size();
                result.prefix_length = prefix_length;
                this->scan_prefix(prefix, result.outside_scan, result.inside_scan);
                result.ambiguous = this->is_ambiguous(result.outside_scan, result.inside_scan);
                result.assumed_start_state = this->choose_start_state(result);
                return result;
            }

        private:
            CONSTEXPR_17 ParseFlags parse_flag(const char ch) const noexcept {
                return parse_flags_.data()[ch + CHAR_OFFSET];
            }

            static constexpr size_t missing_index() noexcept {
                return (std::numeric_limits<size_t>::max)();
            }

            struct PrefixCounterState {
                PrefixScanResult result;
                size_t record_start = 0;
                size_t quoted_start = missing_index();
                bool quoted_field_partial = false;
                long double separator_weight = 0;
            };

            void observe_quoted_span(
                PrefixCounterState& state,
                size_t field_length
            ) const noexcept {
                state.result.quoted_fields++;

                if (field_length == 0) {
                    return;
                }

                if (state.quoted_field_partial || field_length == 1) {
                    state.separator_weight += 1;
                }
                else {
                    state.separator_weight += 2;
                }
            }

            void finish_record(PrefixCounterState& state, size_t record_end) const noexcept {
                state.result.records_seen++;
                const size_t record_length = record_end - state.record_start;
                if (record_length > state.result.max_record_length) {
                    state.result.max_record_length = record_length;
                }
                state.record_start = record_end;
            }

            bool is_separator_or_record_end(ParseFlags flag) const noexcept {
                return flag == ParseFlags::DELIMITER
                    || flag == ParseFlags::CARRIAGE_RETURN
                    || flag == ParseFlags::NEWLINE;
            }

            bool quote_can_open(csv::string_view prefix, size_t pos) const noexcept {
                if (pos == 0) {
                    return true;
                }

                return this->is_separator_or_record_end(this->parse_flag(prefix[pos - 1]));
            }

            bool quote_can_close(csv::string_view prefix, size_t pos) const noexcept {
                if (pos + 1 >= prefix.size()) {
                    return true;
                }

                return this->is_separator_or_record_end(this->parse_flag(prefix[pos + 1]));
            }

            void start_quoted_span(PrefixCounterState& state, size_t pos, bool partial) const noexcept {
                state.quoted_start = pos;
                state.quoted_field_partial = partial;
            }

            void finish_quoted_span(PrefixCounterState& state, size_t pos) const noexcept {
                if (state.quoted_start == this->missing_index()) {
                    return;
                }

                this->observe_quoted_span(state, pos - state.quoted_start);
                state.quoted_start = this->missing_index();
                state.quoted_field_partial = false;
            }

            void finish_partial_quoted_span(PrefixCounterState& state, size_t prefix_size) const noexcept {
                if (state.quoted_start == this->missing_index()) {
                    return;
                }

                this->observe_quoted_span(state, prefix_size - state.quoted_start);
                state.quoted_start = this->missing_index();
                state.quoted_field_partial = false;
            }

            void finish_scan(
                PrefixCounterState& state,
                csv::string_view prefix,
                bool ending_state,
                long double log_separator_probability
            ) const noexcept {
                const size_t tail_length = prefix.size() - state.record_start;
                if (tail_length > state.result.max_record_length) {
                    state.result.max_record_length = tail_length;
                }
                state.result.ending_state = ParserDFAState(ending_state);
                state.result.log_other_start_valid_probability = state.separator_weight == 0
                    ? 0
                    : state.separator_weight * log_separator_probability;
            }

            // The speculative pass should be much cheaper than parsing. This
            // is a quote-parity scan: q-o / o-q pattern evidence usually
            // decides the starting state, and the probability model is left
            // for the rare ambiguous prefix.
            void scan_prefix(
                csv::string_view prefix,
                PrefixScanResult& outside_scan,
                PrefixScanResult& inside_scan
            ) const {
                using internals::ParseFlags;

                PrefixCounterState outside;
                PrefixCounterState inside;
                this->start_quoted_span(inside, 0, true);

                bool odd_quotes = false;
                size_t separators = 0;

                for (size_t pos = 0; pos < prefix.size();) {
                    const ParseFlags flag = this->parse_flag(prefix[pos]);
                    size_t width = 1;
                    if (flag == ParseFlags::CARRIAGE_RETURN
                        && pos + 1 < prefix.size()
                        && this->parse_flag(prefix[pos + 1]) == ParseFlags::NEWLINE) {
                        width = 2;
                    }

                    outside.result.total_states += width;
                    inside.result.total_states += width;
                    if (!odd_quotes) {
                        outside.result.unquoted_states += width;
                    }
                    else {
                        inside.result.unquoted_states += width;
                    }

                    if (flag == ParseFlags::DELIMITER || flag == ParseFlags::CARRIAGE_RETURN || flag == ParseFlags::NEWLINE) {
                        separators++;
                    }

                    if (flag == ParseFlags::QUOTE) {
                        if (outside.result.first_quote_open == this->missing_index()
                            && this->quote_can_open(prefix, pos)) {
                            outside.result.first_quote_open = pos;
                        }
                        if (inside.result.first_quote_close == this->missing_index()
                            && this->quote_can_close(prefix, pos)) {
                            inside.result.first_quote_close = pos;
                        }

                        if (odd_quotes) {
                            this->finish_quoted_span(outside, pos);
                            this->start_quoted_span(inside, pos + 1, false);
                        }
                        else {
                            this->start_quoted_span(outside, pos + 1, false);
                            this->finish_quoted_span(inside, pos);
                        }

                        odd_quotes = !odd_quotes;
                        pos++;
                        continue;
                    }

                    if (flag == ParseFlags::CARRIAGE_RETURN || flag == ParseFlags::NEWLINE) {
                        const size_t record_end = pos + width;
                        if (!odd_quotes) {
                            this->finish_record(outside, record_end);
                            if (outside.result.first_record_end == this->missing_index()) {
                                outside.result.first_record_end = record_end;
                            }
                        }
                        else {
                            this->finish_record(inside, record_end);
                            if (inside.result.first_record_end == this->missing_index()) {
                                inside.result.first_record_end = record_end;
                            }
                        }
                    }

                    pos += width;
                }

                const long double separator_probability = prefix.empty()
                    ? 0
                    : static_cast<long double>(separators) / static_cast<long double>(prefix.size());
                const long double log_separator_probability = separator_probability > 0
                    ? std::log(separator_probability)
                    : -std::numeric_limits<long double>::infinity();

                if (odd_quotes) {
                    this->finish_partial_quoted_span(outside, prefix.size());
                }
                else {
                    this->finish_partial_quoted_span(inside, prefix.size());
                }

                this->finish_scan(outside, prefix, odd_quotes, log_separator_probability);
                this->finish_scan(inside, prefix, !odd_quotes, log_separator_probability);
                outside_scan = outside.result;
                inside_scan = inside.result;
            }

            long double unquoted_ratio(const PrefixScanResult& scan) const noexcept {
                if (scan.total_states == 0) {
                    return scan.ending_state.quote_escape ? 0 : 1;
                }

                return static_cast<long double>(scan.unquoted_states)
                    / static_cast<long double>(scan.total_states);
            }

            long double quoted_odds_from_probability_model(
                const PrefixScanResult& outside_scan,
                const PrefixScanResult& inside_scan
            ) const noexcept {
                const long double log_k = inside_scan.log_other_start_valid_probability
                    - outside_scan.log_other_start_valid_probability;

                if (log_k > 64) {
                    return std::numeric_limits<long double>::infinity();
                }
                if (log_k < -64) {
                    return 0;
                }

                const long double k = std::exp(log_k);
                const long double u_u = this->unquoted_ratio(outside_scan);
                const long double u_q = this->unquoted_ratio(inside_scan);
                const long double a = u_q;
                const long double b = u_u - k * (1 - u_q);
                const long double c = -k * (1 - u_u);
                const long double epsilon = 1e-18L;

                if (std::fabs(a) < epsilon) {
                    if (std::fabs(b) < epsilon) {
                        return k > 1 ? std::numeric_limits<long double>::infinity() : 0;
                    }

                    const long double linear_root = -c / b;
                    return linear_root > 0 ? linear_root : 0;
                }

                const long double discriminant = b * b - 4 * a * c;
                if (discriminant < 0) {
                    return 1;
                }

                const long double root = (-b + std::sqrt(discriminant)) / (2 * a);
                return root > 0 ? root : 0;
            }

            int pattern_start_state(
                const PrefixScanResult& outside_scan,
                const PrefixScanResult& inside_scan
            ) const noexcept {
                const size_t outside_open = outside_scan.first_quote_open;
                const size_t inside_close = inside_scan.first_quote_close;
                const size_t missing = (std::numeric_limits<size_t>::max)();

                if (outside_open == missing && inside_close == missing) {
                    return -1;
                }

                if (outside_open < inside_close) {
                    return 0;
                }

                if (inside_close < outside_open) {
                    return 1;
                }

                return -1;
            }

            ParserDFAState choose_start_state(ChunkSpeculation& speculation) const noexcept {
                const PrefixScanResult& outside_scan = speculation.outside_scan;
                const PrefixScanResult& inside_scan = speculation.inside_scan;
                const int pattern_state = this->pattern_start_state(outside_scan, inside_scan);

                if (pattern_state == 0) {
                    return ParserDFAState(false);
                }

                if (pattern_state == 1) {
                    return ParserDFAState(true);
                }

                if (outside_scan.records_seen > 0 && inside_scan.records_seen == 0) {
                    return ParserDFAState(false);
                }

                if (inside_scan.records_seen > 0 && outside_scan.records_seen == 0) {
                    return ParserDFAState(true);
                }

                if (speculation.ambiguous) {
                    speculation.quoted_start_odds = this->quoted_odds_from_probability_model(
                        outside_scan,
                        inside_scan
                    );

                    if (speculation.quoted_start_odds > 1.000001L) {
                        speculation.used_probability_model = true;
                        return ParserDFAState(true);
                    }

                    if (speculation.quoted_start_odds < 0.999999L) {
                        speculation.used_probability_model = true;
                        return ParserDFAState(false);
                    }

                    speculation.used_record_size_heuristic = true;
                    return inside_scan.max_record_length < outside_scan.max_record_length
                        ? ParserDFAState(true)
                        : ParserDFAState(false);
                }

                return ParserDFAState(false);
            }

            bool is_ambiguous(
                const PrefixScanResult& outside_scan,
                const PrefixScanResult& inside_scan
            ) const noexcept {
                if (this->pattern_start_state(outside_scan, inside_scan) != -1) {
                    return false;
                }

                return (outside_scan.records_seen == inside_scan.records_seen)
                    || (outside_scan.records_seen > 0 && inside_scan.records_seen > 0);
            }

            ParseFlagMap parse_flags_;
            size_t prefix_bytes_;
        };
        }
    }
}

#endif
