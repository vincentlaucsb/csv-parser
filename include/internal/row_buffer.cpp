/** @file
 *  Defines an object which can store CSV data in
 *  continuous regions of memory
 */

#include "row_buffer.hpp"

namespace csv {
    namespace internals {
        //////////////
        // ColNames //
        //////////////

        CSV_INLINE ColNames::ColNames(const std::vector<std::string>& _cnames)
            : col_names(_cnames) {
            for (size_t i = 0; i < _cnames.size(); i++) {
                this->col_pos[_cnames[i]] = i;
            }
        }

        CSV_INLINE std::vector<std::string> ColNames::get_col_names() const {
            return this->col_names;
        }

        CSV_INLINE size_t ColNames::size() const {
            return this->col_names.size();
        }

        CSV_INLINE csv::string_view RawRowBuffer::get_row() {
            csv::string_view ret(
                this->buffer.c_str() + this->current_end, // Beginning of string
                (this->buffer.size() - this->current_end) // Count
            );

            this->current_end = this->buffer.size();
            return ret;
        }

        CSV_INLINE ColumnPositions RawRowBuffer::get_splits()
        {
            const size_t head_idx = this->current_split_idx,
                new_split_idx = this->split_buffer.size();
         
            this->current_split_idx = new_split_idx;
            return ColumnPositions(*this, head_idx, (unsigned short)(new_split_idx - head_idx + 1));
        }

        CSV_INLINE size_t RawRowBuffer::size() const {
            return this->buffer.size() - this->current_end;
        }

        CSV_INLINE size_t RawRowBuffer::splits_size() const {
            return this->split_buffer.size() - this->current_split_idx;
        }
        
        HEDLEY_WARN_UNUSED_RESULT CSV_INLINE
        BufferPtr RawRowBuffer::reset() const {
            // Save current row in progress
            auto new_buff = BufferPtr(new RawRowBuffer());

            // Save text
            new_buff->buffer = this->buffer.substr(
                this->current_end,   // Position
                (this->buffer.size() - this->current_end) // Count
            );

            // Save split buffer in progress
            for (size_t i = this->current_split_idx; i < this->split_buffer.size(); i++) {
                new_buff->split_buffer.push_back(this->split_buffer[i]);
            }

            new_buff->col_names = this->col_names;

            // No need to remove unnecessary bits from this buffer
            // (memory savings would be marginal anyways)
            return new_buff;
        }

        CSV_INLINE unsigned short ColumnPositions::split_at(int n) const {
            return this->parent->split_buffer[this->start + n];
        }
    }
}