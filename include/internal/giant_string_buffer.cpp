#include "giant_string_buffer.hpp"
#include "giant_string_buffer.hpp"
#include "giant_string_buffer.hpp"

namespace csv {
    namespace internals {
        //////////////
        // ColNames //
        //////////////

        ColNames::ColNames(const std::vector<std::string>& _cnames)
            : col_names(_cnames) {
            for (size_t i = 0; i < _cnames.size(); i++) {
                this->col_pos[_cnames[i]] = i;
            }
        }

        std::vector<std::string> ColNames::get_col_names() const {
            return this->col_names;
        }

        size_t ColNames::size() const {
            return this->col_names.size();
        }

        /**
         * Return a string_view over the current_row
         */
        csv::string_view RawRowBuffer::get_row() {
            csv::string_view ret(
                this->buffer.c_str() + this->current_end, // Beginning of string
                (this->buffer.size() - this->current_end) // Count
            );

            this->current_end = this->buffer.size();
            return ret;
        }

        ColumnPositions RawRowBuffer::get_splits()
        {
            const size_t head_idx = this->current_split_idx,
                new_split_idx = this->split_buffer.size();
         
            this->current_split_idx = new_split_idx;
            return ColumnPositions(*this, head_idx, new_split_idx - head_idx + 1);
        }

        /** Return size of current row */
        size_t RawRowBuffer::size() const {
            return this->buffer.size() - this->current_end;
        }

        /** Return (num columns - 1) for current row */
        size_t RawRowBuffer::splits_size() const {
            return this->split_buffer.size() - this->current_split_idx;
        }
        
        /** Clear out the buffer, but save current row in progress */
        std::shared_ptr<RawRowBuffer> RawRowBuffer::reset() const {
            // Save current row in progress
            auto new_buff = std::shared_ptr<RawRowBuffer>(new RawRowBuffer());

            new_buff->buffer = this->buffer.substr(
                this->current_end,   // Position
                (this->buffer.size() - this->current_end) // Count
            );

            new_buff->col_names = this->col_names;

            // No need to remove unnecessary bits from this buffer
            // (memory savings would be marginal anyways)
            return new_buff;
        }

        unsigned short ColumnPositions::operator[](int n) const {
            return this->parent->split_buffer[this->start + n];
        }
    }
}