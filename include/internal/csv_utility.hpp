#pragma once
#include "compatibility.hpp"
#include "constants.hpp"
#include "data_type.h"

#include <string>
#include <type_traits>
#include <unordered_map>

namespace csv {
    /** Returned by get_file_info() */
    struct CSVFileInfo {
        std::string filename;               /**< Filename */
        std::vector<std::string> col_names; /**< CSV column names */
        char delim;                         /**< Delimiting character */
        RowCount n_rows;                    /**< Number of rows in a file */
        int n_cols;                         /**< Number of columns in a CSV */
    };

    /** @name Shorthand Parsing Functions
     *  @brief Convienience functions for parsing small strings
     */
     ///@{
    CSVCollection operator ""_csv(const char*, size_t);
    CSVCollection parse(csv::string_view in, CSVFormat format = CSVFormat());
    ///@}

    /** @name Utility Functions */
    ///@{
    std::unordered_map<std::string, DataType> csv_data_types(const std::string&);
    CSVFileInfo get_file_info(const std::string& filename);
    CSVGuessResult guess_format(csv::string_view filename,
        const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' });
    std::vector<std::string> get_col_names(
        const std::string& filename,
        const CSVFormat format = CSVFormat::GUESS_CSV);
    int get_col_pos(const std::string filename, const std::string col_name,
        const CSVFormat format = CSVFormat::GUESS_CSV);
    ///@}

    namespace internals {
        template<typename T>
        inline bool is_equal(T a, T b, T epsilon = 0.001) {
            /** Returns true if two floating point values are about the same */
            static_assert(std::is_floating_point<T>::value, "T must be a floating point type.");
            return std::abs(a - b) < epsilon;
        }
    }
}