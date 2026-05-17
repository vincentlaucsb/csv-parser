#pragma once

#include "../parallel/indexed_task_pool.hpp"
#include "scanner.hpp"
#include "validator.hpp"

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

        template<bool EagerClassify = false>
        class ParallelCSVParser {
        public:
            ParallelCSVParser(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags,
                size_t worker_count = 1,
                const ColNamesPtr& col_names = nullptr
            ) : parse_flags_(parse_flags),
                ws_flags_(ws_flags),
                col_names_(col_names),
                task_pool_(worker_count == 0 ? 1 : worker_count) {
                this->init_worker_parsers();
            }

            ~ParallelCSVParser() {}

            ParallelCSVParser(const ParallelCSVParser&) = delete;
            ParallelCSVParser& operator=(const ParallelCSVParser&) = delete;

            ParsedChunkRows parse_chunk(const SpeculativeParseChunk& chunk) const {
                ChunkParserCoreT<EagerClassify> parser(this->parse_flags_, this->ws_flags_, this->col_names_);
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

                ChunkParserCoreT<EagerClassify> repair_parser(this->parse_flags_, this->ws_flags_, this->col_names_);
                SpeculativeParseValidator<RowSink, ChunkParserCoreT<EagerClassify>> validator(repair_parser, output);
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
                ChunkParserCoreT<EagerClassify>& parser,
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
                if (this->task_pool_.worker_count() > 1 && chunks.size() > 1) {
                    this->parse_chunks_parallel(chunks, parsed);
                    return;
                }

                ChunkParserCoreT<EagerClassify> parser(this->parse_flags_, this->ws_flags_, this->col_names_);
                for (size_t i = 0; i < chunks.size(); ++i) {
                    parsed[i] = this->parse_chunk_with(parser, chunks[i]);
                }
            }

            void parse_chunks_parallel(
                const std::vector<SpeculativeParseChunk>& chunks,
                std::vector<ParsedChunkRows>& parsed
            ) {
                this->task_pool_.parallel_for(chunks.size(), [this, &chunks, &parsed](
                    size_t worker_index,
                    size_t task_index
                ) {
                    CSV_DEBUG_ASSERT(worker_index < this->worker_parsers_.size());
                    parsed[task_index] = this->parse_chunk_with(
                        this->worker_parsers_[worker_index],
                        chunks[task_index]
                    );
                });
            }

            void init_worker_parsers() {
                const size_t worker_count = this->task_pool_.worker_count();
                if (worker_count <= 1) {
                    return;
                }

                this->worker_parsers_.reserve(worker_count);
                for (size_t i = 0; i < worker_count; ++i) {
                    this->worker_parsers_.push_back(
                        ChunkParserCoreT<EagerClassify>(this->parse_flags_, this->ws_flags_, this->col_names_)
                    );
                }
            }

            ParseFlagMap parse_flags_;
            WhitespaceMap ws_flags_;
            ColNamesPtr col_names_;
            internals::parallel::IndexedTaskPool task_pool_;
            std::vector<ChunkParserCoreT<EagerClassify>> worker_parsers_;
        };
        }
    }
}

#endif
