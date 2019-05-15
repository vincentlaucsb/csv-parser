#include "giant_string_buffer.hpp"
#include "giant_string_buffer.hpp"
#include "giant_string_buffer.hpp"

namespace csv {
    namespace internals {
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
            ColumnPositions pos(*this);
            size_t head_idx = this->current_split_idx,
                size = this->split_buffer.size();
            this->current_split_idx = size;

            pos.start = head_idx;
            pos.size = size - head_idx + 1;
            return pos;
        }

        /** Return size of current row */
        size_t RawRowBuffer::size() const {
            return this->buffer.size() - this->current_end;
        }

        /** Return number of columns minus one of current row */
        size_t RawRowBuffer::splits_size() const {
            return this->split_buffer.size() - this->current_split_idx;
        }
        
        /** Clear out the buffer, but save current row in progress */
        std::shared_ptr<RawRowBuffer> RawRowBuffer::reset() {
            // Save current row in progress
            auto new_buff = std::shared_ptr<RawRowBuffer>(new RawRowBuffer());

            new_buff->buffer = this->buffer.substr(
                this->current_end,   // Position
                (this->buffer.size() - this->current_end) // Count
            );

            // No need to remove unnecessary bits from this buffer

            return new_buff;
        }

        unsigned short ColumnPositions::operator[](int n) const {
            return this->parent->split_buffer[this->start + n];
        }
    }
}