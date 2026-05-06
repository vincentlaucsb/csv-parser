#pragma once

#include "csv_chunk_parser.hpp"
#include "csv_speculative_diagnostics.hpp"

#include <cmath>
#include <limits>

#if CSV_ENABLE_THREADS
#include <condition_variable>
#include <thread>
#endif

namespace csv {
    namespace internals {
#if CSV_ENABLE_THREADS
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

        class SpeculativeParseValidator {
        public:
            SpeculativeParseValidator(
                CSVParserCore& repair_parser,
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

            CSVParserCore& repair_parser_;
            CSVRowSink& output_;
            ParserDFAState expected_start_state_;
            CSVRowFragment pending_suffix_;
            size_t repair_count_ = 0;
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
                // boundary because source windows must be aligned to incomplete-row
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
                worker_count_(worker_count == 0 ? 1 : worker_count) {
                this->start_workers();
            }

            ~ParallelCSVParser() {
                this->stop_workers();
            }

            ParallelCSVParser(const ParallelCSVParser&) = delete;
            ParallelCSVParser& operator=(const ParallelCSVParser&) = delete;

            ParsedChunkRows parse_chunk(const SpeculativeParseChunk& chunk) const {
                ChunkParserCore parser(this->parse_flags_, this->ws_flags_);
                return this->parse_chunk_with(parser, chunk);
            }

            ParallelCSVParseResult parse_chunks(
                const std::vector<SpeculativeParseChunk>& chunks,
                CSVRowSink& output,
                bool finish = true
            ) {
                ParallelCSVParseResult result;
                result.chunks_processed = chunks.size();
                for (size_t i = 0; i < chunks.size(); ++i) {
                    observe_speculation(result.diagnostics, chunks[i].speculation);
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
                CSVParserCore& parser,
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
            ) {
                if (this->worker_count_ > 1 && chunks.size() > 1) {
                    this->parse_chunks_parallel(chunks, parsed);
                    return;
                }

                ChunkParserCore parser(this->parse_flags_, this->ws_flags_);
                for (size_t i = 0; i < chunks.size(); ++i) {
                    parsed[i] = this->parse_chunk_with(parser, chunks[i]);
                }
            }

            void parse_chunks_parallel(
                const std::vector<SpeculativeParseChunk>& chunks,
                std::vector<ParsedChunkRows>& parsed
            ) {
                if (this->workers_.empty()) {
                    ChunkParserCore parser(this->parse_flags_, this->ws_flags_);
                    for (size_t i = 0; i < chunks.size(); ++i) {
                        parsed[i] = this->parse_chunk_with(parser, chunks[i]);
                    }
                    return;
                }

                std::exception_ptr worker_exception;
                {
                    std::unique_lock<std::mutex> lock(this->work_mutex_);
                    this->active_chunks_ = &chunks;
                    this->active_parsed_ = &parsed;
                    this->next_task_ = 0;
                    this->completed_workers_ = 0;
                    this->failed_ = false;
                    this->worker_exception_ = nullptr;
                    this->generation_++;
                }

                this->work_ready_.notify_all();

                {
                    std::unique_lock<std::mutex> lock(this->work_mutex_);
                    this->work_done_.wait(lock, [this]() {
                        return this->completed_workers_ == this->workers_.size();
                    });
                    worker_exception = this->worker_exception_;
                    this->active_chunks_ = nullptr;
                    this->active_parsed_ = nullptr;
                }

                if (worker_exception) {
                    std::rethrow_exception(worker_exception);
                }
            }

            void start_workers() {
                if (this->worker_count_ <= 1) {
                    return;
                }

                this->workers_.reserve(this->worker_count_);
                for (size_t i = 0; i < this->worker_count_; ++i) {
                    this->workers_.push_back(std::thread(&ParallelCSVParser::worker_loop, this));
                }
            }

            void stop_workers() {
                {
                    std::lock_guard<std::mutex> lock(this->work_mutex_);
                    this->stop_ = true;
                    this->generation_++;
                }
                this->work_ready_.notify_all();

                for (size_t i = 0; i < this->workers_.size(); ++i) {
                    if (this->workers_[i].joinable()) {
                        this->workers_[i].join();
                    }
                }
            }

            void worker_loop() {
                ChunkParserCore parser(this->parse_flags_, this->ws_flags_);
                size_t observed_generation = 0;

                for (;;) {
                    {
                        std::unique_lock<std::mutex> lock(this->work_mutex_);
                        this->work_ready_.wait(lock, [this, &observed_generation]() {
                            return this->stop_ || this->generation_ != observed_generation;
                        });

                        if (this->stop_) {
                            return;
                        }
                        observed_generation = this->generation_;
                    }

                    for (;;) {
                        size_t task = 0;
                        const std::vector<SpeculativeParseChunk>* chunks = nullptr;
                        std::vector<ParsedChunkRows>* parsed = nullptr;
                        {
                            std::lock_guard<std::mutex> lock(this->work_mutex_);
                            if (this->failed_ || this->next_task_ >= this->active_chunks_->size()) {
                                break;
                            }

                            task = this->next_task_++;
                            chunks = this->active_chunks_;
                            parsed = this->active_parsed_;
                        }

                        try {
                            (*parsed)[task] = this->parse_chunk_with(parser, (*chunks)[task]);
                        }
                        catch (...) {
                            std::lock_guard<std::mutex> lock(this->work_mutex_);
                            this->failed_ = true;
                            if (!this->worker_exception_) {
                                this->worker_exception_ = std::current_exception();
                            }
                            break;
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(this->work_mutex_);
                        this->completed_workers_++;
                        if (this->completed_workers_ == this->workers_.size()) {
                            this->work_done_.notify_one();
                        }
                    }
                }
            }

            ParseFlagMap parse_flags_;
            WhitespaceMap ws_flags_;
            size_t worker_count_;
            std::vector<std::thread> workers_;
            std::mutex work_mutex_;
            std::condition_variable work_ready_;
            std::condition_variable work_done_;
            const std::vector<SpeculativeParseChunk>* active_chunks_ = nullptr;
            std::vector<ParsedChunkRows>* active_parsed_ = nullptr;
            size_t next_task_ = 0;
            size_t completed_workers_ = 0;
            size_t generation_ = 0;
            bool stop_ = false;
            bool failed_ = false;
            std::exception_ptr worker_exception_;
        };
#endif

        class CSVParseOrchestrator : public ICSVParseOrchestrator {
        public:
            CSVParseOrchestrator(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags,
                const CSVFormat& format,
                size_t source_size,
                const ColNamesPtr& col_names = nullptr,
                bool enable_speculative_parallel = true,
                bool source_size_known = true
            )
                : serial_parser_(parse_flags, ws_flags, col_names)
#if CSV_ENABLE_THREADS
                  , parse_flags_(parse_flags),
                  ws_flags_(ws_flags),
                  scanner_(parse_flags)
#endif
            {
#if CSV_ENABLE_THREADS
                size_t n_threads = 1;
                if (enable_speculative_parallel
                    && format.is_speculative_parallel_enabled()
                    && (!source_size_known || source_size >= format.get_speculative_parallel_min_bytes())) {
                    n_threads = format.get_speculative_parallel_threads();
                    if (n_threads == 0) {
                        const unsigned int hardware_threads = std::thread::hardware_concurrency();
                        n_threads = hardware_threads == 0 ? 2 : static_cast<size_t>(hardware_threads);
                    }
                }

                this->worker_count_ = n_threads == 0 ? 1 : n_threads;
                this->use_speculative_parallel_ = enable_speculative_parallel
                    && format.is_speculative_parallel_enabled()
                    && this->worker_count_ > 1
                    && (!source_size_known || format.should_use_speculative_parallel(
                        source_size,
                        this->worker_count_
                    ));
                if (this->use_speculative_parallel_) {
                    this->speculative_parser_.reset(new ParallelCSVParser(
                        this->parse_flags_,
                        this->ws_flags_,
                        this->worker_count_
                    ));
                }
#else
                (void)parse_flags;
                (void)ws_flags;
                (void)format;
                (void)source_size;
                (void)enable_speculative_parallel;
                (void)source_size_known;
#endif
            }

            size_t worker_count() const noexcept override {
#if CSV_ENABLE_THREADS
                return this->use_speculative_parallel_ ? this->worker_count_ : 1;
#else
                return 1;
#endif
            }

            SpeculativeParseDiagnostics diagnostics() const noexcept override {
                return this->speculative_diagnostics_;
            }

            size_t read_window_size(size_t chunk_size) const noexcept override {
#if CSV_ENABLE_THREADS
                if (!this->use_speculative_parallel_ || this->worker_count_ <= 1) {
                    return chunk_size;
                }

                const size_t max_size = (std::numeric_limits<size_t>::max)();
                if (chunk_size > max_size / this->worker_count_) {
                    return max_size;
                }

                return chunk_size * this->worker_count_;
#else
                return chunk_size;
#endif
            }

            bool utf8_bom() const noexcept override {
                return this->serial_parser_.utf8_bom();
            }

            void reset_with_initial_state(ParserDFAState state) noexcept override {
                this->serial_parser_.reset_with_initial_state(state);
            }

            ParserDFAState ending_state() const noexcept override {
                return this->serial_parser_.ending_state();
            }

            CSVParseWindowResult parse_window(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                size_t base_offset,
                size_t serial_chunk_size,
                bool source_exhausted,
                CSVRowSink& output
            ) override {
#if CSV_ENABLE_THREADS
                if (this->use_speculative_parallel_
                    && this->worker_count_ > 1
                    && serial_chunk_size > 0
                    && chunk.size() > serial_chunk_size) {
                    return this->parse_speculative_window(
                        chunk,
                        std::move(owner),
                        base_offset,
                        serial_chunk_size,
                        source_exhausted,
                        output
                    );
                }
#else
                (void)base_offset;
                (void)serial_chunk_size;
#endif

                return this->parse_serial_window(
                    chunk,
                    std::move(owner),
                    source_exhausted,
                    output
                );
            }

        private:
            CSVParseWindowResult parse_serial_window(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                bool source_exhausted,
                CSVRowSink& output
            ) {
                CSVParseWindowResult result;
                const ParserChunkResult parse_result = this->serial_parser_.parse_chunk(
                    chunk,
                    std::move(owner),
                    output
                );
                result.complete_prefix_length = parse_result.complete_prefix_length;
                if (source_exhausted) {
                    this->serial_parser_.end_feed();
                }
                return result;
            }

#if CSV_ENABLE_THREADS
            CSVParseWindowResult parse_speculative_window(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                size_t base_offset,
                size_t serial_chunk_size,
                bool source_exhausted,
                CSVRowSink& output
            ) {
                CSVParseWindowResult result;
                auto chunks = make_speculative_parse_chunks(
                    chunk,
                    owner,
                    serial_chunk_size,
                    this->scanner_,
                    base_offset,
                    0,
                    base_offset == 0
                );

                const ParallelCSVParseResult parse_result = this->speculative_parser_->parse_chunks(
                    chunks,
                    output,
                    source_exhausted
                );

                result.complete_prefix_length = parse_result.complete_prefix_length;
                this->speculative_diagnostics_.merge(parse_result.diagnostics);
                return result;
            }
#endif

            CSVParserCore serial_parser_;
            SpeculativeParseDiagnostics speculative_diagnostics_;
#if CSV_ENABLE_THREADS
            ParseFlagMap parse_flags_;
            WhitespaceMap ws_flags_;
            SpeculativeScanner scanner_;
            bool use_speculative_parallel_ = false;
            size_t worker_count_ = 1;
            std::unique_ptr<ParallelCSVParser> speculative_parser_;
#endif
        };

        inline std::unique_ptr<ICSVParseOrchestrator> make_csv_parse_orchestrator(
            const ParseFlagMap& parse_flags,
            const WhitespaceMap& ws_flags,
            const CSVFormat& format,
            size_t source_size,
            const ColNamesPtr& col_names,
            bool enable_speculative_parallel,
            bool source_size_known
        ) {
            return std::unique_ptr<ICSVParseOrchestrator>(new CSVParseOrchestrator(
                parse_flags,
                ws_flags,
                format,
                source_size,
                col_names,
                enable_speculative_parallel,
                source_size_known
            ));
        }
    }
}
