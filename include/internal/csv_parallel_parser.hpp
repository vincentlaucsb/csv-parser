#pragma once

#include "speculative/scanner.hpp"
#include "speculative/validator.hpp"

#if CSV_ENABLE_THREADS
#include <condition_variable>
#include <thread>
#endif

#if CSV_ENABLE_THREADS

namespace csv {
    namespace internals {
        namespace speculative {
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

            template<typename RowSink>
            ParallelCSVParseResult parse_chunks(
                const std::vector<SpeculativeParseChunk>& chunks,
                RowSink& output,
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
                SpeculativeParseValidator<RowSink> validator(repair_parser, output);
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
                CSVParserCore<std::vector<CSVRow>>& parser,
                const SpeculativeParseChunk& chunk
            ) const {
                std::vector<CSVRow> rows;
                const ParserChunkResult parse_result = parser.parse_chunk(
                    chunk.bytes,
                    chunk.owner,
                    rows,
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
        }
    }
}

#endif
