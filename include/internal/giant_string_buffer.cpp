#include "giant_string_buffer.hpp"

namespace csv {
    namespace internals {
        /**
         * Return a string_view over the current_row
         */
        csv::string_view GiantStringBuffer::get_row() {
            csv::string_view ret(
                this->buffer->c_str() + this->current_end, // Beginning of string
                (this->buffer->size() - this->current_end) // Count
            );

            this->current_end = this->buffer->size();
            return ret;
        }

        /** Return size of current row */
        size_t GiantStringBuffer::size() const {
            return (this->buffer->size() - this->current_end);
        }

        std::string* GiantStringBuffer::get() const {
            return this->buffer.get();
        }

        std::string* GiantStringBuffer::operator->() const {
            return this->buffer.operator->();
        }
        
        /** Clear out the buffer, but save current row in progress */
        void GiantStringBuffer::reset() {
            // Save current row in progress
            auto temp_str = this->buffer->substr(
                this->current_end,   // Position
                (this->buffer->size() - this->current_end) // Count
            );

            this->current_end = 0;
            this->buffer = std::make_shared<std::string>(temp_str);
        }
    }
}