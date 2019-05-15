#pragma once
#include <memory>
#include <vector>
#include <unordered_map>

#include "compatibility.hpp" // For string view

namespace csv {
    namespace internals {
        class RawRowBuffer;
        struct ColumnPositions;
        using BufferPtr = std::shared_ptr<RawRowBuffer>;

        /** @struct ColNames
         *  @brief A data structure for handling column name information.
         *
         *  These are created by CSVReader and passed (via smart pointer)
         *  to CSVRow objects it creates, thus
         *  allowing for indexing by column name.
         */
        struct ColNames {
            ColNames(const std::vector<std::string>&);
            std::vector<std::string> col_names;
            std::unordered_map<std::string, size_t> col_pos;

            std::vector<std::string> get_col_names() const;
            size_t size() const;
        };

        /** Class for reducing number of new string malloc() calls */
        class RawRowBuffer {
        public:
            csv::string_view get_row();
            ColumnPositions get_splits();

            size_t size() const;
            size_t splits_size() const;
            BufferPtr reset() const;

            std::string buffer;
            std::vector<unsigned short> split_buffer = {};
            std::shared_ptr<internals::ColNames> col_names = nullptr;

        private:
            size_t current_end = 0;
            size_t current_split_idx = 0;
        };

        struct ColumnPositions {
            ColumnPositions() : parent(nullptr) {};
            constexpr ColumnPositions(const RawRowBuffer& _parent,
                size_t _start, size_t _size) : parent(&_parent), start(_start), size(_size) {};
            const RawRowBuffer * parent;

            /// Where in split_buffer the array of column positions begins
            size_t start;

            /// Number of columns
            size_t size;

            /// Get the n-th column index
            unsigned short operator[](int n) const;
        };
    }
}