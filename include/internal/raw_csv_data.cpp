/** @file
 *  @brief Implementation of internal CSV data structures
 */

#include "raw_csv_data.hpp"

#include <cassert>

namespace csv {
    namespace internals {
        CSV_INLINE RawCSVField& CSVFieldList::operator[](size_t n) const {
            const size_t page_no = n / _single_buffer_capacity;
            const size_t buffer_idx = n % _single_buffer_capacity;

            assert(page_no < _block_capacity);
            RawCSVField* block = this->_blocks[page_no].load(std::memory_order_acquire);
            assert(block != nullptr);
            return block[buffer_idx];
        }

        CSV_INLINE void CSVFieldList::allocate() {
            if (_back != nullptr) {
                _current_block++;
            }

            std::unique_ptr<RawCSVField[]> block(new RawCSVField[_single_buffer_capacity]);
            RawCSVField* block_ptr = block.get();
            this->_owned_blocks.push_back(std::move(block));

            this->_blocks[_current_block].store(block_ptr, std::memory_order_release);
            _current_buffer_size = 0;
            _back = block_ptr;
        }
    }
}
