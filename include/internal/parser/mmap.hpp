#pragma once

#include "driver.hpp"

#if !defined(__EMSCRIPTEN__)
namespace csv {
    namespace internals {
        namespace parser {
        /** Parser for memory-mapped files.
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
         */
        class MmapParser : public CSVParserDriverBase {
        public:
            MmapParser(
                csv::string_view filename,
                const CSVFormat& format,
                const ColNamesPtr& col_names = nullptr
            );

            ~MmapParser();

            std::string& get_csv_head() override {
                // head_ was already populated in the constructor.
                return this->head_;
            }

            void next(size_t bytes) override;

        private:
            void finalize_loaded_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                size_t length,
                size_t chunk_size
            );

            size_t read_window_size(size_t chunk_size) const noexcept;

            std::string _filename;
            size_t mmap_pos = 0;
            std::string head_;
        };
        }
    }
}
#endif
