/** @file
 *  @brief Contains the main CSV parsing algorithm and various utility functions
 */

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#include "../external/mio.hpp"
#endif
#include "basic_csv_parser_simd.hpp"
#include "col_names.hpp"
#include "common.hpp"
#if CSV_ENABLE_THREADS
#include <atomic>
#include <thread>
#endif
#include "csv_exceptions.hpp"
#include "csv_format.hpp"
#include "csv_row.hpp"
#include "row_deque.hpp"

namespace csv {
    namespace internals {
        constexpr const int UNINITIALIZED_FIELD = -1;

        /** Helper constexpr function to initialize an array with all the elements set to value
         */
        template<typename OutArray, typename T = typename OutArray::type>
        CSV_CONST CONSTEXPR_17 OutArray arrayToDefault(T&& value)
        {
            OutArray a {};
            for (auto& e : a)
                 e = value;
            return a;
        }

        struct GuessScore {
            size_t header;
            size_t mode_row_length;
            double score;
        };

        CSV_INLINE GuessScore calculate_score(csv::string_view head, const CSVFormat& format);

        /** Guess the delimiter used by a delimiter-separated values file. */
        CSV_INLINE CSVGuessResult guess_format(
            csv::string_view head,
            const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' }
        );

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        CSV_CONST CONSTEXPR_17 ParseFlagMap make_parse_flags(char delimiter) {
            auto ret = arrayToDefault<ParseFlagMap>(ParseFlags::NOT_SPECIAL);
            ret[delimiter + CHAR_OFFSET] = ParseFlags::DELIMITER;
            ret['\r' + CHAR_OFFSET] = ParseFlags::CARRIAGE_RETURN;
            ret['\n' + CHAR_OFFSET] = ParseFlags::NEWLINE;
            return ret;
        }

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        CSV_CONST CONSTEXPR_17 ParseFlagMap make_parse_flags(char delimiter, char quote_char) {
            std::array<ParseFlags, 256> ret = make_parse_flags(delimiter);
            ret[quote_char + CHAR_OFFSET] = ParseFlags::QUOTE;
            return ret;
        }

        inline char infer_char_for_flag(
            const ParseFlagMap& parse_flags,
            ParseFlags target,
            char fallback
        ) noexcept {
            for (size_t i = 0; i < parse_flags.size(); ++i) {
                if (parse_flags[i] == target) {
                    return static_cast<char>(static_cast<int>(i) - CHAR_OFFSET);
                }
            }

            return fallback;
        }

        inline char infer_delimiter(const ParseFlagMap& parse_flags) noexcept {
            return infer_char_for_flag(parse_flags, ParseFlags::DELIMITER, ',');
        }

        // fallback is returned when no QUOTE flag exists in parse_flags (e.g. no_quote mode).
        // Pass the delimiter so SIMD stops there instead of on a byte that is NOT_SPECIAL.
        inline char infer_quote_char(const ParseFlagMap& parse_flags, char fallback = '"') noexcept {
            return infer_char_for_flag(parse_flags, ParseFlags::QUOTE, fallback);
        }

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character c and, v[i + 128] is true if
         *  c is a whitespace character
         */
        CSV_CONST CONSTEXPR_17 WhitespaceMap make_ws_flags(const char* ws_chars, size_t n_chars) {
            auto ret = arrayToDefault<WhitespaceMap>(false);
            for (size_t j = 0; j < n_chars; j++) {
                ret[ws_chars[j] + CHAR_OFFSET] = true;
            }
            return ret;
        }

        inline WhitespaceMap make_ws_flags(const std::vector<char>& flags) {
            return make_ws_flags(flags.data(), flags.size());
        }

        /** Return the number of leading BOM bytes to skip, or throw for unsupported Unicode encodings. */
        CSV_INLINE size_t get_bom_skip_or_throw(csv::string_view data, bool& utf8_bom);

        /** Read the first 500KB from a seekless stream source. */
        template<typename TStream,
            csv::enable_if_t<std::is_base_of<std::istream, TStream>::value, int>  = 0>
        std::string get_csv_head_stream(TStream& source) {
            const size_t limit = 500000;
            std::string buf(limit, '\0');
            source.read(&buf[0], (std::streamsize)limit);
            buf.resize(static_cast<size_t>(source.gcount()));
            return buf;
        }

    #if defined(__EMSCRIPTEN__)
        /** Open a file-backed source and read the first 500KB through the stream path. */
        CSV_INLINE std::string get_csv_head_stream(csv::string_view filename);
    #endif

    #if !defined(__EMSCRIPTEN__)
        /** Read the first 500KB from a filename using mmap.
         *  Also returns the total file size so callers avoid a second mmap open.
         */
        CSV_INLINE std::pair<std::string, size_t> get_csv_head_mmap(csv::string_view filename);
    #endif

        /** Compatibility shim selecting stream on Emscripten and mmap otherwise. */
        CSV_INLINE std::string get_csv_head(csv::string_view filename);

        struct ResolvedFormat {
            CSVFormat format;
            size_t n_cols = 0;
        };

        class IBasicCSVParser;
    }

    /** @brief Guess the delimiter, header row, and mode column count of a CSV file
         *
         *  **Heuristic:** For each candidate delimiter, calculate a score based on
         *  the most common row length (mode). The delimiter with the highest score wins.
         *
         *  **Header Detection:**
         *  - If the first row has >= columns than the mode, it's treated as the header
         *  - Otherwise, the first row with the mode length is treated as the header
         *
         *  This approach handles:
         *  - Headers with trailing delimiters or optional columns (wider than data rows)
         *  - Comment lines before the actual header (first row shorter than mode)
         *  - Standard CSVs where first row is the header
         *
        *  @note Score = (row_length � count_of_rows_with_that_length)
        *  @note Also returns inferred mode-width column count (CSVGuessResult::n_cols)
         */
    inline CSVGuessResult guess_format(csv::string_view filename,
        const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' }) {
        auto head = internals::get_csv_head(filename);
        return internals::guess_format(head, delims);
    }

    /** Standard type for storing collection of rows */
    using RowCollection = internals::ThreadSafeDeque<CSVRow>;

    namespace internals {
        class CSVRowSink {
        public:
            virtual ~CSVRowSink() {}
            virtual void push_row(CSVRow&& row) = 0;
        };

        class VectorRowSink : public CSVRowSink {
        public:
            VectorRowSink() = default;
            explicit VectorRowSink(std::vector<CSVRow>& rows) : rows_(&rows) {}

            void reset(std::vector<CSVRow>& rows) noexcept {
                rows_ = &rows;
            }

            void push_row(CSVRow&& row) override {
                rows_->push_back(std::move(row));
            }

        private:
            std::vector<CSVRow>* rows_ = nullptr;
        };

        /** Explicit DFA state at a parse boundary.
         *
         *  This is intentionally about parser control flow, not field metadata.
         *  `pending_quote` represents the ambiguous case where a chunk ended at
         *  a quote while already inside a quoted field; the next byte determines
         *  whether that quote closes the field, escapes a quote, or is kept as
         *  non-strict literal content.
         */
        struct ParserDFAState {
            ParserDFAState() noexcept = default;
            ParserDFAState(
                bool quote_escape,
                bool pending_quote = false,
                bool pending_linefeed = false
            ) noexcept
                : quote_escape(quote_escape),
                  pending_quote(pending_quote),
                  pending_linefeed(pending_linefeed) {}

            bool quote_escape = false;
            bool pending_quote = false;
            bool pending_linefeed = false;
        };

        inline bool parser_dfa_state_equal(ParserDFAState lhs, ParserDFAState rhs) noexcept {
            return lhs.quote_escape == rhs.quote_escape
                && lhs.pending_quote == rhs.pending_quote
                && lhs.pending_linefeed == rhs.pending_linefeed;
        }

        struct ParserChunkOptions {
            ParserChunkOptions() noexcept = default;
            explicit ParserChunkOptions(ParserDFAState initial_state, bool scan_bom = true) noexcept
                : initial_state(initial_state), scan_bom(scan_bom) {}

            ParserDFAState initial_state;
            bool scan_bom = true;
        };

        struct ParserChunkResult {
            ParserChunkResult() noexcept = default;
            ParserChunkResult(
                ParserDFAState initial_state,
                ParserDFAState ending_state,
                size_t complete_prefix_length
            ) noexcept
                : initial_state(initial_state),
                  ending_state(ending_state),
                  complete_prefix_length(complete_prefix_length) {}

            ParserDFAState initial_state;
            ParserDFAState ending_state;
            size_t complete_prefix_length = 0;
        };

        struct CSVRowFragment {
            CSVRowFragment() = default;
            CSVRowFragment(
                csv::string_view bytes,
                std::shared_ptr<void> owner,
                ParserDFAState initial_state = ParserDFAState(),
                ParserDFAState ending_state = ParserDFAState(),
                size_t offset = 0
            ) : bytes(bytes),
                owner(std::move(owner)),
                initial_state(initial_state),
                ending_state(ending_state),
                offset(offset),
                present(true) {}

            bool empty() const noexcept {
                return !present;
            }

            static CSVRowFragment from_row(
                const CSVRow& row,
                ParserDFAState initial_state = ParserDFAState(),
                ParserDFAState ending_state = ParserDFAState(),
                size_t chunk_offset = 0
            ) {
                return CSVRowFragment(
                    row.raw_str(),
                    row.data,
                    initial_state,
                    ending_state,
                    chunk_offset + row.data_start
                );
            }

            csv::string_view bytes;
            std::shared_ptr<void> owner;
            ParserDFAState initial_state;
            ParserDFAState ending_state;
            size_t offset = 0;
            bool present = false;
        };

        inline CSVRowFragment concatenate_row_fragments(
            const CSVRowFragment& left,
            const CSVRowFragment& right
        ) {
            if (left.empty()) {
                return right;
            }

            if (right.empty()) {
                return left;
            }

            auto bytes = std::make_shared<std::string>();
            bytes->reserve(left.bytes.size() + right.bytes.size());
            if (!left.bytes.empty()) {
                bytes->append(left.bytes.data(), left.bytes.size());
            }
            if (!right.bytes.empty()) {
                bytes->append(right.bytes.data(), right.bytes.size());
            }

            return CSVRowFragment(
                csv::string_view(*bytes),
                bytes,
                left.initial_state,
                right.ending_state,
                left.offset
            );
        }

        struct ParsedChunkRows {
            size_t sequence_number = 0;
            size_t offset = 0;
            csv::string_view chunk;
            std::shared_ptr<void> owner;
            bool starts_at_record_boundary = true;
            bool scan_bom = true;
            ParserChunkResult parse_result;
            CSVRowFragment prefix_fragment;
            std::vector<CSVRow> complete_rows;
            CSVRowFragment suffix_fragment;
        };

        inline ParsedChunkRows split_parsed_chunk_rows(
            size_t sequence_number,
            csv::string_view chunk,
            std::shared_ptr<void> owner,
            const ParserChunkResult& parse_result,
            std::vector<CSVRow> parsed_rows,
            bool starts_at_record_boundary,
            size_t chunk_offset = 0
        ) {
            ParsedChunkRows result;
            result.sequence_number = sequence_number;
            result.offset = chunk_offset;
            result.chunk = chunk;
            result.owner = owner;
            result.starts_at_record_boundary = starts_at_record_boundary
                || parse_result.initial_state.pending_linefeed;
            result.scan_bom = starts_at_record_boundary && sequence_number == 0;
            result.parse_result = parse_result;

            size_t first_complete_row = 0;
            if (!result.starts_at_record_boundary) {
                if (parsed_rows.empty()) {
                    result.prefix_fragment = CSVRowFragment(
                        chunk,
                        owner,
                        parse_result.initial_state,
                        parse_result.ending_state,
                        chunk_offset
                    );
                    return result;
                }

                result.prefix_fragment = CSVRowFragment::from_row(
                    parsed_rows.front(),
                    parse_result.initial_state,
                    ParserDFAState(),
                    chunk_offset
                );
                first_complete_row = 1;
            }

            result.complete_rows.reserve(parsed_rows.size() - first_complete_row);
            for (size_t i = first_complete_row; i < parsed_rows.size(); ++i) {
                result.complete_rows.push_back(std::move(parsed_rows[i]));
            }

            if (parse_result.complete_prefix_length < chunk.size()) {
                result.suffix_fragment = CSVRowFragment(
                    chunk.substr(parse_result.complete_prefix_length),
                    owner,
                    ParserDFAState(),
                    parse_result.ending_state,
                    chunk_offset + parse_result.complete_prefix_length
                );
            }

            return result;
        }

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

        struct SpeculativeParseDiagnostics {
            size_t chunks = 0;
            size_t ambiguous_chunks = 0;
            size_t probability_model_chunks = 0;
            size_t record_size_heuristic_chunks = 0;
            size_t assumed_quoted_chunks = 0;
            size_t assumed_unquoted_chunks = 0;
            size_t validation_repairs = 0;

            void observe(const ChunkSpeculation& speculation) noexcept {
                this->chunks++;
                if (speculation.ambiguous) {
                    this->ambiguous_chunks++;
                }
                if (speculation.used_probability_model) {
                    this->probability_model_chunks++;
                }
                if (speculation.used_record_size_heuristic) {
                    this->record_size_heuristic_chunks++;
                }
                if (speculation.assumed_start_state.quote_escape) {
                    this->assumed_quoted_chunks++;
                }
                else {
                    this->assumed_unquoted_chunks++;
                }
            }

            void merge(const SpeculativeParseDiagnostics& other) noexcept {
                this->chunks += other.chunks;
                this->ambiguous_chunks += other.ambiguous_chunks;
                this->probability_model_chunks += other.probability_model_chunks;
                this->record_size_heuristic_chunks += other.record_size_heuristic_chunks;
                this->assumed_quoted_chunks += other.assumed_quoted_chunks;
                this->assumed_unquoted_chunks += other.assumed_unquoted_chunks;
                this->validation_repairs += other.validation_repairs;
            }
        };

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
                const long double separator_probability = this->separator_probability(prefix);
                result.outside_scan = this->scan_prefix(prefix, ParserDFAState(false), separator_probability);
                result.inside_scan = this->scan_prefix(prefix, ParserDFAState(true), separator_probability);
                result.ambiguous = this->is_ambiguous(result.outside_scan, result.inside_scan);
                result.assumed_start_state = this->choose_start_state(result);
                return result;
            }

        private:
            CONSTEXPR_17 ParseFlags parse_flag(const char ch) const noexcept {
                return parse_flags_.data()[ch + CHAR_OFFSET];
            }

            CONSTEXPR_17 ParseFlags compound_parse_flag(const char ch, bool quote_escape) const noexcept {
                return quote_escape_flag(this->parse_flag(ch), quote_escape);
            }

            long double separator_probability(csv::string_view prefix) const noexcept {
                if (prefix.empty()) {
                    return 0;
                }

                size_t separators = 0;
                for (size_t i = 0; i < prefix.size(); ++i) {
                    const ParseFlags flag = this->parse_flag(prefix[i]);
                    if (flag == ParseFlags::DELIMITER
                        || flag == ParseFlags::CARRIAGE_RETURN
                        || flag == ParseFlags::NEWLINE) {
                        separators++;
                    }
                }

                return static_cast<long double>(separators) / static_cast<long double>(prefix.size());
            }

            void observe_state(PrefixScanResult& result, bool quote_escape) const noexcept {
                result.total_states++;
                if (!quote_escape) {
                    result.unquoted_states++;
                }
            }

            void observe_record_byte(size_t& current_record_length, size_t n = 1) const noexcept {
                current_record_length += n;
            }

            void finish_record(PrefixScanResult& result, size_t& current_record_length) const noexcept {
                result.records_seen++;
                if (current_record_length > result.max_record_length) {
                    result.max_record_length = current_record_length;
                }
                current_record_length = 0;
            }

            void observe_quoted_field(
                PrefixScanResult& result,
                size_t field_length,
                bool partial,
                long double log_separator_probability
            ) const noexcept {
                result.quoted_fields++;

                if (field_length == 0) {
                    return;
                }

                if (partial || field_length == 1) {
                    result.log_other_start_valid_probability += log_separator_probability;
                }
                else {
                    result.log_other_start_valid_probability += 2 * log_separator_probability;
                }
            }

            PrefixScanResult scan_prefix(
                csv::string_view prefix,
                ParserDFAState state,
                long double separator_probability
            ) const {
                using internals::ParseFlags;

                PrefixScanResult result;
                bool quote_escape = state.quote_escape;
                bool pending_quote = state.pending_quote;
                bool pending_linefeed = state.pending_linefeed;
                size_t field_length = 0;
                size_t current_record_length = 0;
                size_t quoted_field_length = 0;
                bool tracking_quoted_field = quote_escape;
                bool quoted_field_partial = quote_escape;
                size_t pos = 0;
                const long double log_separator_probability = separator_probability > 0
                    ? std::log(separator_probability)
                    : -std::numeric_limits<long double>::infinity();

                if (pending_linefeed && pos < prefix.size()) {
                    pending_linefeed = false;
                    if (this->parse_flag(prefix[pos]) == ParseFlags::NEWLINE) {
                        this->observe_state(result, quote_escape);
                        this->observe_record_byte(current_record_length);
                        pos++;
                        this->finish_record(result, current_record_length);
                    }
                }

                if (pending_quote && pos < prefix.size()) {
                    const ParseFlags next_ch = this->parse_flag(prefix[pos]);
                    pending_quote = false;

                    if (next_ch >= ParseFlags::DELIMITER) {
                        quote_escape = false;
                        if (result.first_quote_close == (std::numeric_limits<size_t>::max)()) {
                            result.first_quote_close = pos;
                        }
                        if (tracking_quoted_field) {
                            this->observe_quoted_field(
                                result,
                                quoted_field_length,
                                true,
                                log_separator_probability
                            );
                            tracking_quoted_field = false;
                            quoted_field_length = 0;
                        }
                    }
                    else if (next_ch == ParseFlags::QUOTE) {
                        quote_escape = true;
                        field_length++;
                        quoted_field_length++;
                        this->observe_state(result, quote_escape);
                        this->observe_record_byte(current_record_length);
                        pos++;
                    }
                    else {
                        quote_escape = true;
                        field_length++;
                        quoted_field_length++;
                    }
                }

                // This intentionally mirrors only DFA control state, not field
                // materialization. The real parser remains the source of truth
                // for rows; this prefix scan only produces cheap speculation.
                while (pos < prefix.size()) {
                    switch (this->compound_parse_flag(prefix[pos], quote_escape)) {
                    case ParseFlags::DELIMITER:
                        this->observe_state(result, quote_escape);
                        this->observe_record_byte(current_record_length);
                        field_length = 0;
                        pos++;
                        break;

                    case ParseFlags::CARRIAGE_RETURN:
                        this->observe_state(result, quote_escape);
                        this->observe_record_byte(current_record_length);
                        if (pos + 1 == prefix.size()) {
                            pos++;
                            pending_linefeed = true;
                        }
                        else if (this->parse_flag(prefix[pos + 1]) == ParseFlags::NEWLINE) {
                            this->observe_state(result, quote_escape);
                            this->observe_record_byte(current_record_length);
                            pos++;
                            pos++;
                        }
                        else {
                            pos++;
                        }
                        field_length = 0;
                        this->finish_record(result, current_record_length);
                        if (result.first_record_end == (std::numeric_limits<size_t>::max)()) {
                            result.first_record_end = pos;
                        }
                        break;

                    case ParseFlags::NEWLINE:
                        this->observe_state(result, quote_escape);
                        this->observe_record_byte(current_record_length);
                        pos++;
                        field_length = 0;
                        this->finish_record(result, current_record_length);
                        if (result.first_record_end == (std::numeric_limits<size_t>::max)()) {
                            result.first_record_end = pos;
                        }
                        break;

                    case ParseFlags::NOT_SPECIAL:
                        this->observe_state(result, quote_escape);
                        this->observe_record_byte(current_record_length);
                        field_length++;
                        if (tracking_quoted_field) {
                            quoted_field_length++;
                        }
                        pos++;
                        break;

                    case ParseFlags::QUOTE_ESCAPE_QUOTE:
                        this->observe_state(result, quote_escape);
                        this->observe_record_byte(current_record_length);
                        if (pos + 1 == prefix.size()) {
                            pending_quote = true;
                            pos++;
                            break;
                        }
                        else {
                            const ParseFlags next_ch = this->parse_flag(prefix[pos + 1]);
                            if (next_ch >= ParseFlags::DELIMITER) {
                                quote_escape = false;
                                if (result.first_quote_close == (std::numeric_limits<size_t>::max)()) {
                                    result.first_quote_close = pos;
                                }
                                if (tracking_quoted_field) {
                                    this->observe_quoted_field(
                                        result,
                                        quoted_field_length,
                                        quoted_field_partial,
                                        log_separator_probability
                                    );
                                    tracking_quoted_field = false;
                                    quoted_field_partial = false;
                                    quoted_field_length = 0;
                                }
                                pos++;
                                break;
                            }
                            else if (next_ch == ParseFlags::QUOTE) {
                                this->observe_state(result, quote_escape);
                                this->observe_record_byte(current_record_length);
                                field_length += 2;
                                if (tracking_quoted_field) {
                                    quoted_field_length += 2;
                                }
                                pos += 2;
                                break;
                            }
                        }

                        field_length++;
                        if (tracking_quoted_field) {
                            quoted_field_length++;
                        }
                        pos++;
                        break;

                    default:
                        this->observe_state(result, quote_escape);
                        this->observe_record_byte(current_record_length);
                        if (field_length == 0) {
                            quote_escape = true;
                            if (result.first_quote_open == (std::numeric_limits<size_t>::max)()) {
                                result.first_quote_open = pos;
                            }
                            tracking_quoted_field = true;
                            quoted_field_partial = false;
                            quoted_field_length = 0;
                            pos++;
                            break;
                        }

                        field_length++;
                        if (tracking_quoted_field) {
                            quoted_field_length++;
                        }
                        pos++;
                        break;
                    }
                }

                if (tracking_quoted_field) {
                    this->observe_quoted_field(
                        result,
                        quoted_field_length,
                        true,
                        log_separator_probability
                    );
                }
                if (current_record_length > result.max_record_length) {
                    result.max_record_length = current_record_length;
                }
                result.ending_state = ParserDFAState(quote_escape, pending_quote, pending_linefeed);
                return result;
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

        /** Abstract base class which provides CSV parsing logic.
         *
         *  Concrete implementations may customize this logic across
         *  different input sources, such as memory mapped files, stringstreams,
         *  etc...
         */
        class IBasicCSVParser {
        public:
            IBasicCSVParser() = default;
            IBasicCSVParser(const CSVFormat&, const ColNamesPtr&);
            IBasicCSVParser(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags
            ) : parse_flags_(parse_flags), ws_flags_(ws_flags) {
                const char d = internals::infer_delimiter(parse_flags);
                simd_sentinels_ = SentinelVecs(d, internals::infer_quote_char(parse_flags, d));
                has_ws_trimming_ = std::any_of(ws_flags.begin(), ws_flags.end(), [](bool b) { return b; });
            }

            virtual ~IBasicCSVParser() {}

            /** Whether or not we have reached the end of source */
            bool eof() { return this->eof_; }

            ResolvedFormat get_resolved_format() { return this->format; }

            /** Parse the next block of data */
            virtual void next(size_t bytes) = 0;

            /** Indicate the last block of data has been parsed */
            void end_feed();

            CONSTEXPR_17 ParseFlags parse_flag(const char ch) const noexcept {
                return parse_flags_.data()[ch + CHAR_OFFSET];
            }

            CONSTEXPR_17 ParseFlags compound_parse_flag(const char ch) const noexcept {
                return quote_escape_flag(parse_flag(ch), this->quote_escape_);
            }

            /** Whether or not this CSV has a UTF-8 byte order mark */
            CONSTEXPR bool utf8_bom() const { return this->utf8_bom_; }

            virtual SpeculativeParseDiagnostics speculative_diagnostics() const noexcept {
                return SpeculativeParseDiagnostics();
            }

            void set_output(RowCollection& rows) {
                this->records_ = &rows;
                this->row_sink_ = nullptr;
            }

            void set_output(CSVRowSink& sink) noexcept {
                this->records_ = nullptr;
                this->row_sink_ = &sink;
            }

            /** Seed the DFA state for the next parse call. */
            void reset_with_initial_state(ParserDFAState state) noexcept {
                this->initial_state_ = state;
            }

            /** Convenience overload for callers that only care about quote state. */
            void reset_with_initial_state(bool starts_in_quoted, bool in_escape = false) noexcept {
                this->reset_with_initial_state(ParserDFAState{ starts_in_quoted, in_escape });
            }

            /** Return the DFA state observed when the most recent parse call stopped. */
            ParserDFAState ending_state() const noexcept {
                return this->ending_state_;
            }

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner
            );

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                const ParserChunkOptions& options
            );

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                CSVRowSink& sink
            );

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                CSVRowSink& sink,
                const ParserChunkOptions& options
            );

        protected:
            /** @name Current Parser State */
            ///@{
            CSVRow current_row_;
            RawCSVDataPtr data_ptr_ = nullptr;
            ColNamesPtr col_names_ = nullptr;
            RawCSVFieldList* fields_ = nullptr;
            int field_start_ = UNINITIALIZED_FIELD;
            size_t field_length_ = 0;

            /** Precomputed SIMD broadcast vectors for find_next_non_special */
            SentinelVecs simd_sentinels_;

            /** An array where the (i + 128)th slot gives the ParseFlags for ASCII character i */
            ParseFlagMap parse_flags_;
            ///@}

            /** @name Current Stream/File State */
            ///@{
            bool eof_ = false;

            ResolvedFormat format;

            /** The size of the incoming CSV */
            size_t source_size_ = 0;
            ///@}

            virtual std::string& get_csv_head() = 0;

            /** Parse the current chunk of data and return the completed-row prefix length. */
            size_t parse();

            /** Create a new RawCSVDataPtr for a new chunk of data */
            void reset_data_ptr();

            void resolve_format_from_head(const CSVFormat& format);

            const WhitespaceMap& whitespace_flags() const noexcept {
                return this->ws_flags_;
            }

            void emit_row(CSVRow&& row) {
                if (this->records_) {
                    this->records_->push_back(std::move(row));
                }
                else {
                    this->row_sink_->push_row(std::move(row));
                }
            }
        private:
            /** An array where the (i + 128)th slot determines whether ASCII character i should
             *  be trimmed
             */
            WhitespaceMap ws_flags_;

            /** True when at least one whitespace trim character is configured.
             *  Used to skip trim loops entirely in the common no-trim case.
             */
            bool has_ws_trimming_ = false;
            bool quote_escape_ = false;
            bool pending_quote_ = false;
            bool pending_linefeed_ = false;
            bool field_has_double_quote_ = false;
            ParserDFAState initial_state_;
            ParserDFAState ending_state_;
            bool scan_bom_for_current_chunk_ = true;

            /** Where we are in the current data block */
            size_t data_pos_ = 0;

            /** Whether or not an attempt to find Unicode BOM has been made */
            bool unicode_bom_scan_ = false;
            bool utf8_bom_ = false;

            /** Where serial parser rows should be pushed to. */
            RowCollection* records_ = nullptr;

            /** Alternate sink for speculative/private row collection. */
            CSVRowSink* row_sink_ = nullptr;

            CONSTEXPR_17 bool ws_flag(const char ch) const noexcept {
                return ws_flags_.data()[ch + CHAR_OFFSET];
            }

            size_t& current_row_start() {
                return this->current_row_.data_start;
            }

            void parse_field() noexcept;

            /** Finish parsing the current field */
            void push_field();

            /** Finish parsing the current record and reset row-local state. */
            void finish_row(size_t raw_end);

            /** Finish parsing the current row */
            void push_row(size_t raw_end);

            /** Handle possible Unicode byte order mark */
            void strip_unicode_bom();

            ParserChunkResult parse_prepared_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                const ParserChunkOptions& options
            );

            void resolve_pending_quote_at_start(csv::string_view in);
            void resolve_pending_linefeed_at_start(csv::string_view in);

            ParserDFAState current_dfa_state() const noexcept {
                return ParserDFAState{ this->quote_escape_, this->pending_quote_, this->pending_linefeed_ };
            }

            size_t finish_parse(size_t remainder) noexcept {
                this->ending_state_ = this->current_dfa_state();
                this->initial_state_ = this->ending_state_.pending_linefeed
                    ? this->ending_state_
                    : ParserDFAState{};
                this->scan_bom_for_current_chunk_ = true;
                return remainder;
            }
        };

        inline std::vector<CSVRow> materialize_row_fragment(
            IBasicCSVParser& parser,
            const CSVRowFragment& fragment
        ) {
            std::vector<CSVRow> rows;
            if (fragment.empty()) {
                return rows;
            }

            VectorRowSink sink(rows);
            parser.parse_chunk(
                fragment.bytes,
                fragment.owner,
                sink,
                ParserChunkOptions(ParserDFAState(), false)
            );
            parser.end_feed();
            return rows;
        }

        inline ParsedChunkRows repair_parsed_chunk_rows(
            IBasicCSVParser& parser,
            const ParsedChunkRows& chunk,
            ParserDFAState corrected_initial_state
        ) {
            std::vector<CSVRow> parsed_rows;
            VectorRowSink sink(parsed_rows);

            const ParserChunkResult parse_result = parser.parse_chunk(
                chunk.chunk,
                chunk.owner,
                sink,
                ParserChunkOptions(corrected_initial_state, chunk.scan_bom)
            );

            return split_parsed_chunk_rows(
                chunk.sequence_number,
                chunk.chunk,
                chunk.owner,
                parse_result,
                std::move(parsed_rows),
                chunk.starts_at_record_boundary,
                chunk.offset
            );
        }

        class SpeculativeParseValidator {
        public:
            SpeculativeParseValidator(
                IBasicCSVParser& repair_parser,
                CSVRowSink& output,
                ParserDFAState initial_state = ParserDFAState()
            ) : repair_parser_(repair_parser),
                output_(output),
                expected_start_state_(initial_state) {}

            ParserChunkResult validate_and_release(ParsedChunkRows chunk) {
                if (!parser_dfa_state_equal(chunk.parse_result.initial_state, this->expected_start_state_)) {
                    chunk = repair_parsed_chunk_rows(
                        this->repair_parser_,
                        chunk,
                        this->expected_start_state_
                    );
                    this->repair_count_++;
                }

                const ParserChunkResult parse_result = chunk.parse_result;
                this->release(std::move(chunk));
                return parse_result;
            }

            void finish(bool flush_pending = true) {
                if (flush_pending) {
                    this->release_pending_suffix();
                }
            }

            size_t repair_count() const noexcept {
                return this->repair_count_;
            }

            ParserDFAState expected_start_state() const noexcept {
                return this->expected_start_state_;
            }

            bool has_pending_suffix() const noexcept {
                return !this->pending_suffix_.empty();
            }

            const CSVRowFragment& pending_suffix() const noexcept {
                return this->pending_suffix_;
            }

        private:
            void release(ParsedChunkRows chunk) {
                if (!chunk.prefix_fragment.empty()) {
                    if (!this->pending_suffix_.empty()) {
                        this->pending_suffix_ = concatenate_row_fragments(
                            this->pending_suffix_,
                            chunk.prefix_fragment
                        );
                    }
                    else {
                        this->pending_suffix_ = chunk.prefix_fragment;
                    }

                    if (chunk.parse_result.complete_prefix_length == 0) {
                        this->expected_start_state_ = chunk.parse_result.ending_state;
                        return;
                    }

                    this->release_pending_suffix();
                }

                for (auto& row : chunk.complete_rows) {
                    this->output_.push_row(std::move(row));
                }

                this->pending_suffix_ = chunk.suffix_fragment;
                this->expected_start_state_ = chunk.parse_result.ending_state;
            }

            void release_pending_suffix() {
                if (this->pending_suffix_.empty()) {
                    return;
                }

                auto rows = materialize_row_fragment(this->repair_parser_, this->pending_suffix_);
                for (auto& row : rows) {
                    this->output_.push_row(std::move(row));
                }

                this->pending_suffix_ = CSVRowFragment();
            }

            IBasicCSVParser& repair_parser_;
            CSVRowSink& output_;
            ParserDFAState expected_start_state_;
            CSVRowFragment pending_suffix_;
            size_t repair_count_ = 0;
        };

        /** Minimal parser shell for caller-owned chunks.
         *
         *  The SIGMOD-style speculative path treats input sourcing as an
         *  external concern. This parser core only needs delimiter/whitespace
         *  state and the shared DFA implementation from IBasicCSVParser.
         */
        class ChunkParserCore : public IBasicCSVParser {
        public:
            ChunkParserCore(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags
            ) : IBasicCSVParser(parse_flags, ws_flags) {}

            void next(size_t) override {}

        private:
            std::string& get_csv_head() override {
                return this->empty_head_;
            }

            std::string empty_head_;
        };

        struct SpeculativeParseChunk {
            size_t sequence_number = 0;
            size_t offset = 0;
            csv::string_view bytes;
            std::shared_ptr<void> owner;
            ChunkSpeculation speculation;
            bool starts_at_record_boundary = false;
            bool scan_bom = false;
        };

        struct ParallelCSVParseResult {
            size_t chunks_processed = 0;
            size_t repair_count = 0;
            size_t complete_prefix_length = 0;
            bool has_pending_suffix = false;
            ParserDFAState ending_state;
            SpeculativeParseDiagnostics diagnostics;
        };

        inline std::vector<SpeculativeParseChunk> make_speculative_parse_chunks(
            csv::string_view data,
            std::shared_ptr<void> owner,
            size_t chunk_size,
            const SpeculativeScanner& scanner,
            size_t base_offset = 0,
            size_t first_sequence_number = 0,
            bool scan_bom_for_first_chunk = true
        ) {
            std::vector<SpeculativeParseChunk> chunks;
            if (chunk_size == 0) {
                return chunks;
            }

            size_t sequence_number = first_sequence_number;
            for (size_t offset = 0; offset < data.size(); offset += chunk_size) {
                const size_t length = std::min(chunk_size, data.size() - offset);
                const csv::string_view bytes(data.data() + offset, length);
                const bool first_chunk = offset == 0;

                SpeculativeParseChunk chunk;
                chunk.sequence_number = sequence_number;
                chunk.offset = base_offset + offset;
                chunk.bytes = bytes;
                chunk.owner = owner;
                chunk.speculation = scanner.speculate(sequence_number, chunk.offset, bytes);
                chunk.starts_at_record_boundary = first_chunk;
                chunk.scan_bom = first_chunk && scan_bom_for_first_chunk;

                // The first chunk in a speculative window starts at a known row
                // boundary because mmap windows are re-aligned to incomplete-row
                // starts before the next read.
                if (first_chunk) {
                    chunk.speculation.assumed_start_state = ParserDFAState();
                }

                chunks.push_back(chunk);
                sequence_number++;
            }

            return chunks;
        }

        class ParallelCSVParser {
        public:
            ParallelCSVParser(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags,
                size_t worker_count = 1
            ) : parse_flags_(parse_flags),
                ws_flags_(ws_flags),
                worker_count_(worker_count == 0 ? 1 : worker_count) {}

            ParsedChunkRows parse_chunk(const SpeculativeParseChunk& chunk) const {
                ChunkParserCore parser(this->parse_flags_, this->ws_flags_);
                return this->parse_chunk_with(parser, chunk);
            }

            ParallelCSVParseResult parse_chunks(
                const std::vector<SpeculativeParseChunk>& chunks,
                CSVRowSink& output,
                bool finish = true
            ) const {
                ParallelCSVParseResult result;
                result.chunks_processed = chunks.size();
                for (size_t i = 0; i < chunks.size(); ++i) {
                    result.diagnostics.observe(chunks[i].speculation);
                }

                std::vector<ParsedChunkRows> parsed(chunks.size());
                this->parse_chunks_into(chunks, parsed);

                ChunkParserCore repair_parser(this->parse_flags_, this->ws_flags_);
                SpeculativeParseValidator validator(repair_parser, output);
                for (size_t i = 0; i < parsed.size(); ++i) {
                    validator.validate_and_release(std::move(parsed[i]));
                }
                validator.finish(finish);

                result.repair_count = validator.repair_count();
                result.diagnostics.validation_repairs += result.repair_count;
                result.has_pending_suffix = validator.has_pending_suffix();
                result.ending_state = validator.expected_start_state();
                if (!chunks.empty()) {
                    if (validator.has_pending_suffix()) {
                        result.complete_prefix_length = validator.pending_suffix().offset - chunks.front().offset;
                    }
                    else {
                        result.complete_prefix_length =
                            chunks.back().offset + chunks.back().bytes.size() - chunks.front().offset;
                    }
                }
                return result;
            }

        private:
            ParsedChunkRows parse_chunk_with(
                IBasicCSVParser& parser,
                const SpeculativeParseChunk& chunk
            ) const {
                std::vector<CSVRow> rows;
                VectorRowSink sink(rows);
                const ParserChunkResult parse_result = parser.parse_chunk(
                    chunk.bytes,
                    chunk.owner,
                    sink,
                    ParserChunkOptions(chunk.speculation.assumed_start_state, chunk.scan_bom)
                );

                ParsedChunkRows result = split_parsed_chunk_rows(
                    chunk.sequence_number,
                    chunk.bytes,
                    chunk.owner,
                    parse_result,
                    std::move(rows),
                    chunk.starts_at_record_boundary,
                    chunk.offset
                );
                result.scan_bom = chunk.scan_bom;
                return result;
            }

            void parse_chunks_into(
                const std::vector<SpeculativeParseChunk>& chunks,
                std::vector<ParsedChunkRows>& parsed
            ) const {
#if CSV_ENABLE_THREADS
                if (this->worker_count_ > 1 && chunks.size() > 1) {
                    this->parse_chunks_parallel(chunks, parsed);
                    return;
                }
#endif

                ChunkParserCore parser(this->parse_flags_, this->ws_flags_);
                for (size_t i = 0; i < chunks.size(); ++i) {
                    parsed[i] = this->parse_chunk_with(parser, chunks[i]);
                }
            }

#if CSV_ENABLE_THREADS
            void parse_chunks_parallel(
                const std::vector<SpeculativeParseChunk>& chunks,
                std::vector<ParsedChunkRows>& parsed
            ) const {
                const size_t n_workers = std::min(this->worker_count_, chunks.size());
                std::atomic<size_t> next_task(0);
                std::atomic<bool> failed(false);
                std::exception_ptr worker_exception;
                std::mutex exception_lock;
                std::vector<std::thread> workers;
                workers.reserve(n_workers);

                for (size_t i = 0; i < n_workers; ++i) {
                    workers.push_back(std::thread([this, &chunks, &parsed, &next_task, &failed, &worker_exception, &exception_lock]() {
                        ChunkParserCore parser(this->parse_flags_, this->ws_flags_);

                        while (!failed.load()) {
                            const size_t task = next_task.fetch_add(1);
                            if (task >= chunks.size()) {
                                break;
                            }

                            try {
                                parsed[task] = this->parse_chunk_with(parser, chunks[task]);
                            }
                            catch (...) {
                                failed.store(true);
                                std::lock_guard<std::mutex> lock(exception_lock);
                                if (!worker_exception) {
                                    worker_exception = std::current_exception();
                                }
                                break;
                            }
                        }
                    }));
                }

                for (size_t i = 0; i < workers.size(); ++i) {
                    if (workers[i].joinable()) {
                        workers[i].join();
                    }
                }

                if (worker_exception) {
                    std::rethrow_exception(worker_exception);
                }
            }
#endif

            ParseFlagMap parse_flags_;
            WhitespaceMap ws_flags_;
            size_t worker_count_;
        };

        /** A class for parsing CSV data from any std::istream, including
         *  non-seekable sources such as pipes and decompression filters.
         *
         *  @par Chunk boundary handling
         *  parse() returns the byte offset of the start of the last incomplete
         *  row in the current chunk (the "remainder"). Rather than seeking back
         *  to re-read those bytes (which requires a seekable stream), they are
         *  saved in `leftover_` and prepended to the next chunk. This is
         *  semantically identical to the old seek-back approach but works on
         *  any istream and avoids the syscall overhead of seekg().
         *
         *  @par Format resolution
         *  The constructor reads a head buffer from the stream via get_csv_head()
         *  and passes it to resolve_format_from_head(), which infers the delimiter
         *  and header row when not explicitly set. The head bytes are stored in
         *  `leftover_` so the first next() call re-parses them without re-reading.
         */
        template<typename TStream>
        class StreamParser: public IBasicCSVParser {
            using RowCollection = ThreadSafeDeque<CSVRow>;

        public:
            StreamParser(TStream& source,
                const CSVFormat& format,
                const ColNamesPtr& col_names = nullptr
            ) : IBasicCSVParser(format, col_names),
                source_(source) {
                this->resolve_format_from_head(format);
            }

            StreamParser(
                TStream& source,
                internals::ParseFlagMap parse_flags,
                internals::WhitespaceMap ws_flags) :
                IBasicCSVParser(parse_flags, ws_flags),
                source_(source)
            {}

            ~StreamParser() {}

            std::string& get_csv_head() override {
                leftover_ = get_csv_head_stream(this->source_);
                return this->leftover_;
            }

            void next(size_t bytes = CSV_CHUNK_SIZE_DEFAULT) override {
                if (this->eof()) return;

                auto chunk_owner = std::make_shared<std::string>();
                auto& chunk = *chunk_owner;

                // Prepend leftover bytes from the previous chunk's incomplete
                // trailing row, then read the next block from the stream.
                // Uses a raw buffer to avoid std::string::resize() zero-fill
                // on the full 10MB chunk size (critical for tiny inputs).
                chunk = std::move(leftover_);
                std::unique_ptr<char[]> buf(new char[bytes]);
                source_.read(buf.get(), (std::streamsize)bytes);

                const size_t n = static_cast<size_t>(source_.gcount());
                
                if (n > 0) chunk.append(buf.get(), n);

                // Check for real I/O errors only (bad bit indicates unrecoverable error).
                // failbit alone is not fatal - it's set on EOF or when requesting bytes
                // beyond available data, which is normal behavior for stringstreams.
                if (source_.bad()) {
                    throw_stream_read_failure();
                }

                const ParserChunkResult result = this->parse_chunk(chunk, chunk_owner);

                if (source_.eof() || chunk.empty()) {
                    this->eof_ = true;
                    this->end_feed();
                }
                else {
                    // Save the tail bytes that begin an incomplete row so they
                    // are prepended to the next chunk (see class-level comment).
                    leftover_ = chunk.substr(result.complete_prefix_length);
                }
            }

        private:
            // Bytes from the previous chunk that form the start of an incomplete
            // row, plus the initial head buffer on the first call.
            std::string leftover_;

            TStream& source_;
        };

#if !defined(__EMSCRIPTEN__)
        /** Parser for memory-mapped files
         *
         *  @par Implementation
         *  This class constructs moving windows over a file to avoid
         *  creating massive memory maps which may require more RAM
         *  than the user has available. It contains logic to automatically
         *  re-align each memory map to the beginning of a CSV row.
         *
         *  @par Head buffer
         *  CSVReader may prime a pre-read head buffer used for format guessing
         *  via prime_head_for_reuse(). When provided, the first next() call
         *  parses that buffer directly, then resumes mmap reads from the proper
         *  file offset while preserving chunk-boundary remainder semantics.
         *
         */
        class MmapParser : public IBasicCSVParser {
        public:
            MmapParser(csv::string_view filename,
                const CSVFormat& format,
                const ColNamesPtr& col_names = nullptr
            ) : IBasicCSVParser(format, col_names) {
                this->_filename = filename.data();
                auto head_and_size = get_csv_head_mmap(filename);
                this->head_ = std::move(head_and_size.first);
                this->source_size_ = head_and_size.second;
                this->resolve_format_from_head(format);

                size_t n_threads = 1;
                if (format.is_speculative_parallel_enabled()
                    && this->source_size_ >= format.get_speculative_parallel_min_bytes()) {
#if CSV_ENABLE_THREADS
                    n_threads = format.get_speculative_parallel_threads();
                    if (n_threads == 0) {
                        const unsigned int hardware_threads = std::thread::hardware_concurrency();
                        n_threads = hardware_threads == 0 ? 2 : static_cast<size_t>(hardware_threads);
                    }
#endif
                }
                this->speculative_worker_count_ = n_threads == 0 ? 1 : n_threads;
                this->use_speculative_parallel_ = format.should_use_speculative_parallel(
                    this->source_size_,
                    this->speculative_worker_count_
                );
            };

            ~MmapParser() {}

            std::string& get_csv_head() override {
                // head_ was already populated in the constructor.
                return this->head_;
            }

            void next(size_t bytes) override;

            SpeculativeParseDiagnostics speculative_diagnostics() const noexcept override {
                return this->speculative_diagnostics_;
            }

        private:
            void finalize_loaded_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                size_t length
            );

            void finalize_speculative_loaded_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                size_t length,
                size_t chunk_size
            );

            size_t read_window_size(size_t chunk_size) const noexcept;

            std::string _filename;
            size_t mmap_pos = 0;
            std::string head_;
            bool use_speculative_parallel_ = false;
            size_t speculative_worker_count_ = 1;
            SpeculativeParseDiagnostics speculative_diagnostics_;
        };
#endif
    }
}
