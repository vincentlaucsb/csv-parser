#pragma once

#include "../csv_parallel_parser.hpp"
#include "driver.hpp"

namespace csv {
    namespace internals {
        namespace parser {
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
                if (format.is_threading_enabled()
                    && enable_speculative_parallel
                    && format.is_speculative_parallel_enabled()
                    && (!source_size_known || source_size >= format.get_speculative_parallel_min_bytes())) {
                    n_threads = format.get_speculative_parallel_threads();
                    if (n_threads == 0) {
                        const unsigned int hardware_threads = std::thread::hardware_concurrency();
                        n_threads = hardware_threads == 0 ? 2 : static_cast<size_t>(hardware_threads);
                    }
                }

                this->worker_count_ = n_threads == 0 ? 1 : n_threads;
                this->use_speculative_parallel_ = format.is_threading_enabled()
                    && enable_speculative_parallel
                    && format.is_speculative_parallel_enabled()
                    && this->worker_count_ > 1
                    && (!source_size_known || format.should_use_speculative_parallel(
                        source_size,
                        this->worker_count_
                    ));
                if (this->use_speculative_parallel_) {
                    this->speculative_parser_.reset(new speculative::ParallelCSVParser(
                        this->parse_flags_,
                        this->ws_flags_,
                        this->worker_count_,
                        col_names
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
                RowCollection& output
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
                RowCollection& output
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
                RowCollection& output
            ) {
                CSVParseWindowResult result;
                auto chunks = speculative::make_speculative_parse_chunks(
                    chunk,
                    owner,
                    serial_chunk_size,
                    this->scanner_,
                    base_offset,
                    0,
                    base_offset == 0
                );

                const speculative::ParallelCSVParseResult parse_result = this->speculative_parser_->parse_chunks(
                    chunks,
                    output,
                    source_exhausted
                );

                result.complete_prefix_length = parse_result.complete_prefix_length;
                this->speculative_diagnostics_.merge(parse_result.diagnostics);
                return result;
            }
#endif

            CSVParserCore<> serial_parser_;
            SpeculativeParseDiagnostics speculative_diagnostics_;
#if CSV_ENABLE_THREADS
            ParseFlagMap parse_flags_;
            WhitespaceMap ws_flags_;
            speculative::SpeculativeScanner scanner_;
            bool use_speculative_parallel_ = false;
            size_t worker_count_ = 1;
            std::unique_ptr<speculative::ParallelCSVParser> speculative_parser_;
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
}
