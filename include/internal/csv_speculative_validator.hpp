#pragma once

#include "csv_speculative_chunks.hpp"

#if CSV_ENABLE_THREADS

namespace csv {
    namespace internals {
        namespace speculative {
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
        }
    }
}

#endif
