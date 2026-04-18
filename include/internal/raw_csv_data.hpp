/** @file
 *  @brief Internal data structures for CSV parsing
 * 
 *  This file contains the low-level structures used by the parser to store
 *  CSV data before it's exposed through the public CSVRow/CSVField API.
 * 
 *  Data flow: Parser → RawCSVData → CSVRow → CSVField
 */

#pragma once
#include <cassert>
#include <memory>
#if !defined(CSV_ENABLE_THREADS) || CSV_ENABLE_THREADS
#include <mutex>
#endif
#include <unordered_map>
#include <string>
#include <vector>

#include "common.hpp"
#include "col_names.hpp"

namespace csv {
    namespace internals {
        /** A barebones class used for describing CSV fields */
        struct RawCSVField {
            RawCSVField() = default;
            RawCSVField(size_t _start, size_t _length, bool _double_quote = false) noexcept {
                start = _start;
                length = _length;
                has_double_quote = _double_quote;
            }

            /** The start of the field, relative to the beginning of the row */
            size_t start;

            /** The length of the row, ignoring quote escape characters */
            size_t length; 

            /** Whether or not the field contains an escaped quote */
            bool has_double_quote;
        };

        /** A class used for efficiently storing RawCSVField objects and expanding as necessary
         *
         *  @par Implementation
         *  Stores fields in page-aligned chunks (~170 fields/chunk) via a vector of
         *  unique_ptr<RawCSVField[]>:
         *   - This design provides better cache locality when accessing sequential fields in a row
         *     as well as much lower memory allocation overhead.
         *   - The unique_ptr ensures STL container does not invalidate pointers to fields when resizing,
         *     which is critical to ensure memory safety and correctness of the parser.
         * 
         *  @par Thread Safety
         *  Cross-thread visibility is provided by the records queue mutex in
         *  ThreadSafeDeque: the writer enqueues a RawCSVData only after all fields are
         *  written, and the reader dequeues it only after the mutex unlock/lock pair,
         *  which is a full happens-before edge. No additional atomics are needed here.
         *
         *  @par Historical Bug (Issue #278, fixed Feb 2026)
         *  Move constructor previously left _back pointing to moved-from buffer memory, causing
         *  memory corruption on next emplace_back(). Fixed by recalculating _back from
         *  _owned_blocks after move.
         */
        class RawCSVFieldList {
        public:
            /** Construct a RawCSVFieldList which allocates blocks of a certain size */
            RawCSVFieldList(size_t single_buffer_capacity = (size_t)(internals::PAGE_SIZE / sizeof(RawCSVField))) :
                _single_buffer_capacity(single_buffer_capacity) {
                const size_t max_fields = internals::CSV_CHUNK_SIZE_DEFAULT + 1;
                const size_t block_capacity = (max_fields + _single_buffer_capacity - 1) / _single_buffer_capacity;
                _owned_blocks.reserve(block_capacity);

                this->allocate();
            }

            // No copy constructor
            RawCSVFieldList(const RawCSVFieldList& other) = delete;

            // CSVFieldArrays may be moved
            RawCSVFieldList(RawCSVFieldList&& other) noexcept:
                _single_buffer_capacity(other._single_buffer_capacity) {

                this->_owned_blocks = std::move(other._owned_blocks);
                _current_buffer_size = other._current_buffer_size;
                _current_block = other._current_block;

                // Recalculate _back pointer to point into OUR blocks, not the moved-from ones
                if (!this->_owned_blocks.empty()) {
                    _back = this->_owned_blocks[_current_block].get() + _current_buffer_size;
                } else {
                    _back = nullptr;
                }

                // Invalidate moved-from state to prevent use-after-move bugs
                other._back = nullptr;
                other._current_buffer_size = 0;
                other._current_block = 0;
            }

            template <class... Args>
            void emplace_back(Args&&... args) {
                if (this->_current_buffer_size == this->_single_buffer_capacity) {
                    this->allocate();
                }

                assert(_back != nullptr);
                *(_back++) = RawCSVField(std::forward<Args>(args)...);
                _current_buffer_size++;
            }

            size_t size() const noexcept {
                return this->_current_buffer_size + (_current_block * this->_single_buffer_capacity);
            }

            /** Access a field by its index. This allows CSVRow objects to access fields
             *  without knowing internal implementation details of RawCSVFieldList.
             */
            RawCSVField& operator[](size_t n) const;

        private:
            const size_t _single_buffer_capacity;

            /** Owned field-storage blocks; pre-reserved to avoid reallocation. */
            std::vector<std::unique_ptr<RawCSVField[]>> _owned_blocks = {};

            /** Number of items in the current block */
            size_t _current_buffer_size = 0;

            /** Current block index */
            size_t _current_block = 0;

            /** Pointer to the next empty field slot in the current block */
            RawCSVField* _back = nullptr;

            /** Allocate a new page of memory */
            void allocate();
        };

        /** A class for storing raw CSV data and associated metadata
         * 
         *  This structure is the bridge between the parser thread and the main thread.
         *  Parser populates fields, data, and parse_flags; main thread reads via CSVRow.
         */
        struct RawCSVData {
            std::shared_ptr<void> _data = nullptr;
            csv::string_view data = "";

            internals::RawCSVFieldList fields;

            /** Cached unescaped field values for fields with escaped quotes.
             *  Thread-safe lazy initialization using double-check locking.
             *  Lock is only held during rare concurrent initialization; reads are lock-free.
             */
            std::unordered_map<size_t, std::string> double_quote_fields = {};
#if CSV_ENABLE_THREADS
            mutable std::mutex double_quote_init_lock;  ///< Protects lazy initialization only
#endif

            internals::ColNamesPtr col_names = nullptr;
            internals::ParseFlagMap parse_flags;
            internals::WhitespaceMap ws_flags;

            /** True when at least one whitespace trim character is configured.
             *  Used by get_field_impl() to skip trim work in the common no-trim case.
             */
            bool has_ws_trimming = false;
        };

        using RawCSVDataPtr = std::shared_ptr<RawCSVData>;
    }
}
