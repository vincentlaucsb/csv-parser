#include "giant_string_buffer.hpp"
#include "giant_string_buffer.hpp"
#include "giant_string_buffer.hpp"

namespace csv {
    namespace internals {
        /**
         * Return a string_view over the current_row
         */
        csv::string_view GiantStringBuffer::get_row() {
            csv::string_view ret(
                this->buffer.c_str() + this->current_end, // Beginning of string
                (this->buffer.size() - this->current_end) // Count
            );

            this->current_end = this->buffer.size();
            return ret;
        }

        /** Return size of current row */
        size_t GiantStringBuffer::size() const {
            return this->buffer.size() - this->current_end;
        }
        
        /** Clear out the buffer, but save current row in progress */
        std::shared_ptr<GiantStringBuffer> GiantStringBuffer::reset() {
            // Save current row in progress
            auto new_buff = std::shared_ptr<GiantStringBuffer>(new GiantStringBuffer());

            new_buff->buffer = this->buffer.substr(
                this->current_end,   // Position
                (this->buffer.size() - this->current_end) // Count
            );

            // No need to remove unnecessary bits from this buffer

            return new_buff;
        }

        GiantSplitBuffer::GiantSplitBuffer()
        {
            this->current_head = this->buffer.get();
        }

        ColumnPositions * csv::internals::GiantSplitBuffer::append(std::vector<unsigned short>& in)
        {
            ColumnPositions * const ptr = (ColumnPositions*)this->current_head;

            const size_t arr_size = sizeof(unsigned short) * in.size();
            ptr->n_cols = in.size();
            std::memcpy(ptr->splits, in.data(), arr_size);
            this->current_head += arr_size + sizeof(size_t);

            return ptr;
        }

        void GiantSplitBuffer::reset()
        {
            this->buffer = std::shared_ptr<char[]>(new char[50000]);
        }
    }
}