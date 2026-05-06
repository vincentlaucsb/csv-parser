#include "basic_csv_parser.hpp"
#include "csv_parse_orchestrator.hpp"

#if !defined(__EMSCRIPTEN__)
namespace csv {
    namespace internals {
        CSV_INLINE MmapParser::MmapParser(
            csv::string_view filename,
            const CSVFormat& format,
            const ColNamesPtr& col_names
        ) : CSVParserDriverBase(format, col_names) {
            this->_filename = filename.data();
            auto head_and_size = get_csv_head_mmap(filename);
            this->head_ = std::move(head_and_size.first);
            this->source_size_ = head_and_size.second;
            this->resolve_format_from_head(format);

            this->parse_orchestrator_ = make_csv_parse_orchestrator(
                this->parse_flags_,
                this->whitespace_flags(),
                format,
                this->source_size_,
                col_names,
                true
            );
        }

        CSV_INLINE MmapParser::~MmapParser() = default;

        CSV_INLINE SpeculativeParseDiagnostics MmapParser::speculative_diagnostics() const noexcept {
            return this->parse_orchestrator_
                ? this->parse_orchestrator_->diagnostics()
                : SpeculativeParseDiagnostics();
        }

        CSV_INLINE size_t MmapParser::parse_worker_count() const noexcept {
            return this->parse_orchestrator_
                ? this->parse_orchestrator_->worker_count()
                : 1;
        }

        CSV_INLINE void MmapParser::finalize_loaded_chunk(
            csv::string_view chunk,
            std::shared_ptr<void> owner,
            size_t length,
            size_t chunk_size
        ) {
            const size_t base_offset = this->mmap_pos - length;
            const bool source_exhausted = this->mmap_pos == this->source_size_;
            CSVParseWindowResult result = this->parse_orchestrator_->parse_window(
                chunk,
                std::move(owner),
                base_offset,
                chunk_size,
                source_exhausted,
                this->output()
            );

            if (source_exhausted) {
                this->eof_ = true;
            }

            this->mmap_pos -= (length - result.complete_prefix_length);
        }

        CSV_INLINE size_t MmapParser::read_window_size(size_t chunk_size) const noexcept {
            return this->parse_orchestrator_
                ? this->parse_orchestrator_->read_window_size(chunk_size)
                : chunk_size;
        }

        CSV_INLINE void MmapParser::next(size_t bytes = CSV_CHUNK_SIZE_DEFAULT) {
            // CRITICAL SECTION: Chunk Transition Logic
            // This function reads 10MB chunks and must correctly handle fields that span
            // chunk boundaries. The 'remainder' calculation below ensures partial fields
            // are preserved for the next chunk.
            //
            // Bug #280: Field corruption occurred here when chunk transitions incorrectly
            // split multi-byte characters or field boundaries.

            // Reuse the pre-read head buffer (if any) as the first chunk.
            // This avoids re-reading the same bytes that were already consumed
            // for delimiter/header guessing.
            if (!head_.empty()) {
                auto head_owner = std::make_shared<std::string>(std::move(head_));
                const size_t length = head_owner->size();
                this->mmap_pos += length;

                this->finalize_loaded_chunk(*head_owner, head_owner, length, bytes);
                return;
            }

            // Create memory map
            const size_t offset = this->mmap_pos;
            const size_t remaining = (offset < this->source_size_)
                ? (this->source_size_ - offset)
                : 0;
            const size_t length = std::min(remaining, this->read_window_size(bytes));
            if (length == 0) {
                // No more data to read; mark EOF and end feed
                // (Prevent exception on empty mmap as reported by #267)
                this->eof_ = true;
                this->end_feed();
                return;
            }

            std::error_code error;
            auto mmap = mio::make_mmap_source(this->_filename, offset, length, error);
            if (error) {
                throw_mmap_failure(error, this->_filename, offset, length);
            }
            auto mmap_owner = std::make_shared<mio::basic_mmap_source<char>>(std::move(mmap));
            this->mmap_pos += length;

            auto mmap_ptr = mmap_owner.get();

            // Create string view
            csv::string_view chunk(mmap_ptr->data(), mmap_ptr->length());
            this->finalize_loaded_chunk(chunk, mmap_owner, length, bytes);
        }
    }
}
#endif
