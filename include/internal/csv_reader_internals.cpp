#include "csv_reader_internals.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE size_t get_file_size(csv::string_view filename) {
            std::ifstream infile(std::string(filename), std::ios::binary);
            const auto start = infile.tellg();
            infile.seekg(0, std::ios::end);
            const auto end = infile.tellg();

            return end - start;
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename) {
            return get_csv_head(filename, get_file_size(filename));
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename, size_t file_size) {
            const size_t bytes = 500000;

            std::error_code error;
            size_t length = std::min((size_t)file_size, bytes);
            auto mmap = mio::make_mmap_source(std::string(filename), 0, length, error);

            if (error) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }
            
            return std::string(mmap.begin(), mmap.end());
        }
    }
}