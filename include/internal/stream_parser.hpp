#pragma once

#include "basic_csv_parser.hpp"
#include "csv_speculative_parser.hpp"

namespace csv {
    namespace internals {
        constexpr size_t CSV_STREAM_WINDOW_SIZE_MAX = 256 * 1024 * 1024;

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
        class StreamParser: public CSVParserDriverBase {
            using RowCollection = ThreadSafeDeque<CSVRow>;

        public:
            StreamParser(
                TStream& source,
                const CSVFormat& format,
                const ColNamesPtr& col_names = nullptr
            ) : CSVParserDriverBase(format, col_names),
                source_(source) {
                this->resolve_format_from_head(format);
                this->parse_orchestrator_ = make_csv_parse_orchestrator(
                    this->parse_flags_,
                    this->whitespace_flags(),
                    this->format.format,
                    0,
                    col_names,
                    true,
                    false
                );
            }

            StreamParser(
                TStream& source,
                internals::ParseFlagMap parse_flags,
                internals::WhitespaceMap ws_flags
            ) : CSVParserDriverBase(parse_flags, ws_flags),
                source_(source) {
                this->parse_orchestrator_ = make_csv_parse_orchestrator(
                    this->parse_flags_,
                    this->whitespace_flags(),
                    CSVFormat(),
                    0,
                    nullptr,
                    false,
                    false
                );
            }

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
                // trailing row, then read enough bytes to fill the orchestrator
                // window. The window grows to chunk_size * worker_count only
                // when speculative parsing is active.
                chunk = std::move(leftover_);
                const size_t requested_window_size = this->parse_orchestrator_->read_window_size(bytes);
                const size_t stream_window_cap = bytes > CSV_STREAM_WINDOW_SIZE_MAX
                    ? bytes
                    : CSV_STREAM_WINDOW_SIZE_MAX;
                const size_t window_size = std::min(requested_window_size, stream_window_cap);
                const size_t read_size = chunk.size() < window_size
                    ? window_size - chunk.size()
                    : bytes;
                std::unique_ptr<char[]> buf(new char[read_size]);
                source_.read(buf.get(), (std::streamsize)read_size);

                const size_t n = static_cast<size_t>(source_.gcount());

                if (n > 0) chunk.append(buf.get(), n);

                // Check for real I/O errors only (bad bit indicates unrecoverable error).
                // failbit alone is not fatal - it's set on EOF or when requesting bytes
                // beyond available data, which is normal behavior for stringstreams.
                if (source_.bad()) {
                    throw_stream_read_failure();
                }

                const bool source_exhausted = source_.eof() || chunk.empty();
                const CSVParseWindowResult result = this->parse_orchestrator_->parse_window(
                    chunk,
                    chunk_owner,
                    this->stream_pos_,
                    bytes,
                    source_exhausted,
                    this->output()
                );

                if (source_exhausted) {
                    this->eof_ = true;
                }
                else {
                    // Save the tail bytes that begin an incomplete row so they
                    // are prepended to the next chunk (see class-level comment).
                    leftover_ = chunk.substr(result.complete_prefix_length);
                    this->stream_pos_ += result.complete_prefix_length;
                }
            }

            SpeculativeParseDiagnostics speculative_diagnostics() const noexcept override {
                return this->parse_orchestrator_
                    ? this->parse_orchestrator_->diagnostics()
                    : SpeculativeParseDiagnostics();
            }

            size_t parse_worker_count() const noexcept override {
                return this->parse_orchestrator_
                    ? this->parse_orchestrator_->worker_count()
                    : 1;
            }

            bool utf8_bom() const noexcept override {
                return this->parse_orchestrator_
                    ? this->parse_orchestrator_->utf8_bom()
                    : CSVParserDriverBase::utf8_bom();
            }

            void reset_with_initial_state(ParserDFAState state) noexcept {
                if (this->parse_orchestrator_) {
                    this->parse_orchestrator_->reset_with_initial_state(state);
                }
                else {
                    CSVParserDriverBase::reset_with_initial_state(state);
                }
            }

            void reset_with_initial_state(bool starts_in_quoted, bool in_escape = false) noexcept {
                this->reset_with_initial_state(ParserDFAState{ starts_in_quoted, in_escape });
            }

            ParserDFAState ending_state() const noexcept {
                return this->parse_orchestrator_
                    ? this->parse_orchestrator_->ending_state()
                    : CSVParserDriverBase::ending_state();
            }

        private:
            // Bytes from the previous chunk that form the start of an incomplete
            // row, plus the initial head buffer on the first call.
            std::string leftover_;
            size_t stream_pos_ = 0;

            TStream& source_;
            std::unique_ptr<ICSVParseOrchestrator> parse_orchestrator_;
        };
    }
}
