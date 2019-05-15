#pragma once
#include <memory>
#include <vector>

#include "compatibility.hpp" // For string view

namespace csv {
    namespace internals {
        class GiantStringBuffer;
        using BufferPtr = std::shared_ptr<GiantStringBuffer>;

        /** Class for reducing number of new string malloc() calls */
        class GiantStringBuffer {
        public:
            csv::string_view get_row();
            size_t size() const;
            std::string buffer;
            BufferPtr reset();

        private:
            size_t current_end = 0;
        };

        struct ColumnPositions {
            size_t n_cols;
            unsigned short splits[1];
            
            constexpr unsigned short operator[](size_t n) {
                return this->splits[n];
            }
        };

        /** Class for reducing number of vector malloc() calls */
        class GiantSplitBuffer {
        public:
            GiantSplitBuffer();
            ColumnPositions * append(std::vector<unsigned short>& in);
            void reset();

        private:
            std::shared_ptr<char[]> buffer = std::shared_ptr<char[]>(new char[50000]);
            char * current_head = nullptr;
        };
    }
}