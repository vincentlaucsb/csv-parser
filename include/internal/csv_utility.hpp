#pragma once
#include "compatibility.hpp"
#include "constants.hpp"
#include "csv_format.hpp"
#include "csv_reader.hpp"
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
        size_t n_rows;                      /**< Number of rows in a file */
        int n_cols;                         /**< Number of columns in a CSV */
    };

    /** @name Shorthand Parsing Functions
     *  @brief Convienience functions for parsing small strings
     */
     ///@{
    CSVReader operator ""_csv(const char*, size_t);
    CSVReader parse(csv::string_view in, CSVFormat format = CSVFormat());
    ///@}

    /** @name Utility Functions */
    ///@{
    std::unordered_map<std::string, DataType> csv_data_types(const std::string&);
    CSVFileInfo get_file_info(const std::string& filename);
    int get_col_pos(const std::string filename, const std::string col_name,
        const CSVFormat format = CSVFormat::guess_csv());
    ///@}
}