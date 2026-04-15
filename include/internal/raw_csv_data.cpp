/** @file
 *  @brief Implementation of internal CSV data structures
 */

#include "raw_csv_data.hpp"

#include <cassert>

namespace csv {
    namespace internals {
        CSV_INLINE RawCSVField& RawCSVFieldList::operator[](size_t n) const {
            const size_t page_no = n / _single_buffer_capacity;
            const size_t buffer_idx = n % _single_buffer_capacity;

            return this->_owned_blocks[page_no].get()[buffer_idx];
        }

        CSV_INLINE void RawCSVFieldList::allocate() {
            if (_back != nullptr) {
                _current_block++;
            }

            this->_owned_blocks.push_back(std::make_unique<RawCSVField[]>(_single_buffer_capacity));
            _current_buffer_size = 0;
            _back = this->_owned_blocks.back().get();
        }
    }
}
