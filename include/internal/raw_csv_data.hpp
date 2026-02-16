/** @file
 *  @brief Internal data structures for CSV parsing
 * 
 *  This file contains the low-level structures used by the parser to store
 *  CSV data before it's exposed through the public CSVRow/CSVField API.
 * 
 *  Data flow: Parser → RawCSVData → CSVRow → CSVField
 */

#pragma once
#include <atomic>
#include <memory>
#include <mutex>
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
                const size_t max_fields = internals::ITERATION_CHUNK_SIZE + 1;
                _block_capacity = (max_fields + _single_buffer_capacity - 1) / _single_buffer_capacity;
                _blocks = std::unique_ptr<std::atomic<RawCSVField*>[]>(new std::atomic<RawCSVField*>[_block_capacity]);
                for (size_t i = 0; i < _block_capacity; i++) {
                    _blocks[i].store(nullptr, std::memory_order_relaxed);
                }

                this->allocate();
            }

            // No copy constructor
            CSVFieldList(const CSVFieldList& other) = delete;

            // CSVFieldArrays may be moved
            CSVFieldList(CSVFieldList&& other) :
                _single_buffer_capacity(other._single_buffer_capacity),
                _block_capacity(other._block_capacity) {

                this->_blocks = std::move(other._blocks);
                this->_owned_blocks = std::move(other._owned_blocks);
                _current_buffer_size = other._current_buffer_size;
                _current_block = other._current_block;

                // Recalculate _back pointer to point into OUR blocks, not the moved-from ones
                if (this->_blocks) {
                    RawCSVField* block = this->_blocks[_current_block].load(std::memory_order_acquire);
                    _back = block ? (block + _current_buffer_size) : nullptr;
                } else {
                    _back = nullptr;
                }

                // Invalidate moved-from state to prevent use-after-move bugs
                other._back = nullptr;
                other._current_buffer_size = 0;
                other._current_block = 0;
                other._block_capacity = 0;
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

            /** Fixed-size table of block pointers for lock-free read access. */
            std::unique_ptr<std::atomic<RawCSVField*>[]> _blocks = nullptr;

            /** Owned blocks (writer thread only), used for lifetime management. */
            std::vector<std::unique_ptr<RawCSVField[]>> _owned_blocks = {};
            // _owned_blocks may reallocate, but RawCSVField[] allocations stay put;
            // _blocks holds raw pointers to those allocations, so readers remain valid.

            /** Number of items in the current buffer */
            size_t _current_buffer_size = 0;

            /** Current block index */
            size_t _current_block = 0;

            /** Number of block slots available in _blocks */
            size_t _block_capacity = 0;

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
