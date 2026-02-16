/** @file
 *  @brief Internal data structures for CSV parsing
 * 
 *  This file contains the low-level structures used by the parser to store
 *  CSV data before it's exposed through the public CSVRow/CSVField API.
 * 
 *  Data flow: Parser → RawCSVData → CSVRow → CSVField
 */

#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>

#include "common.hpp"
#include "col_names.hpp"

namespace csv {
    namespace internals {
        /** A barebones class used for describing CSV fields */
        struct RawCSVField {
            RawCSVField() = default;
            RawCSVField(size_t _start, size_t _length, bool _double_quote = false) {
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
         *  Uses std::deque<unique_ptr<RawCSVField[]>> instead of std::deque<RawCSVField> for
         *  performance. This design keeps adjacent fields in page-aligned chunks (~170 fields/chunk),
         *  providing better cache locality when accessing sequential fields in a row.
         *  
         *  Standard std::deque uses smaller, implementation-defined chunks which increases pointer
         *  indirection and reduces cache efficiency for CSV parsing workloads.
         *
         *  @par Thread Safety
         *  This class may be safely read from multiple threads and written to from one,
         *  as long as the writing thread does not actively touch fields which are being
         *  read.
         *
         *  @par Historical Bug (Issue #278, fixed Feb 2026)
         *  Move constructor previously left _back pointing to moved-from buffer memory, causing
         *  memory corruption on next emplace_back(). Now properly recalculates _back pointer
         *  to point into the new buffers after move.
         */
        class CSVFieldList {
        public:
            /** Construct a CSVFieldList which allocates blocks of a certain size */
            CSVFieldList(size_t single_buffer_capacity = (size_t)(internals::PAGE_SIZE / sizeof(RawCSVField))) :
                _single_buffer_capacity(single_buffer_capacity) {
                this->allocate();
            }

            // No copy constructor
            CSVFieldList(const CSVFieldList& other) = delete;

            // CSVFieldArrays may be moved
            CSVFieldList(CSVFieldList&& other) :
                _single_buffer_capacity(other._single_buffer_capacity) {

                this->buffers = std::move(other.buffers);
                _current_buffer_size = other._current_buffer_size;
                _current_block = other._current_block;
                
                // Recalculate _back pointer to point into OUR buffers, not the moved-from ones
                if (!this->buffers.empty()) {
                    _back = this->buffers[_current_block].get() + _current_buffer_size;
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

                *(_back++) = RawCSVField(std::forward<Args>(args)...);
                _current_buffer_size++;
            }

            size_t size() const noexcept {
                return this->_current_buffer_size + (_current_block * this->_single_buffer_capacity);
            }

            RawCSVField& operator[](size_t n) const;

        private:
            const size_t _single_buffer_capacity;

            /** Map of block indices to RawCSVField arrays.
             * 
             *  std::map is ideal for thread-safe concurrent access: insertions never invalidate
             *  references/pointers to existing elements (guaranteed by C++ standard §26.2.6).
             *  This eliminates the need for mutex locks during concurrent read-during-write.
             * 
             *  Unlike std::deque, whose internal map can reallocate (invalidating ALL operator[]
             *  calls even for old elements), std::map provides stable element access.
             * 
             *  Performance: O(log n) access where n = blocks per 10MB chunk. Pathological case
             *  (1-char rows): 10MB / 2 bytes = 5M fields / 170 per block ≈ 29K blocks.
             *  log₂(29K) ≈ 15 comparisons << mutex contention cost.
             * 
             *  See: Issue #217, PR #237, v2.3.0 (June 2024); Sanitizer fixes Feb 2026
             */
            std::map<size_t, std::unique_ptr<RawCSVField[]>> buffers = {};

            /** Number of items in the current buffer */
            size_t _current_buffer_size = 0;

            /** Current block number */
            size_t _current_block = 0;

            /** Pointer to the current empty field */
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

            internals::CSVFieldList fields;

            /** Cached unescaped field values for fields with escaped quotes.
             *  Thread-safe lazy initialization using double-check locking.
             *  Lock is only held during rare concurrent initialization; reads are lock-free.
             */
            std::unordered_map<size_t, std::string> double_quote_fields = {};
            mutable std::mutex double_quote_init_lock;  ///< Protects lazy initialization only

            internals::ColNamesPtr col_names = nullptr;
            internals::ParseFlagMap parse_flags;
            internals::WhitespaceMap ws_flags;
        };

        using RawCSVDataPtr = std::shared_ptr<RawCSVData>;
    }
}
