/** @file
 *  @brief Implementation of internal CSV data structures
 */

#include "raw_csv_data.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE RawCSVField& CSVFieldList::operator[](size_t n) const {
            const size_t page_no = n / _single_buffer_capacity;
            const size_t buffer_idx = (page_no < 1) ? n : n % _single_buffer_capacity;
            return this->buffers[page_no][buffer_idx];
        }

        CSV_INLINE void CSVFieldList::allocate() {
            buffers.push_back(std::unique_ptr<RawCSVField[]>(new RawCSVField[_single_buffer_capacity]));

            _current_buffer_size = 0;
            _back = buffers.back().get();
        }
    }
}
