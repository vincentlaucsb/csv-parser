#pragma once
#include <memory>
#include <vector>

#include "compatibility.hpp" // For string view

namespace csv {
    namespace internals {
        class RawRowBuffer;
        class ColumnPositions;
        using BufferPtr = std::shared_ptr<RawRowBuffer>;

        /** Class for reducing number of new string malloc() calls */
        class RawRowBuffer {
        public:
            csv::string_view get_row();
            ColumnPositions get_splits();

            size_t size() const;
            size_t splits_size() const;

            BufferPtr reset();

            std::string buffer;
            std::vector<unsigned short> split_buffer = {};

        private:
            size_t current_end = 0;
            size_t current_split_idx = 0;
        };

        struct ColumnPositions {
            ColumnPositions() {};
            ColumnPositions(const RawRowBuffer& _parent) : parent(&_parent) {};
            const RawRowBuffer * parent = nullptr;

            /// Where in split_buffer the array of column positions begins
            size_t start;

            /// Number of columns
            size_t size;

            /// Get the n-th column index
            unsigned short operator[](int n) const;
        };
    }
}