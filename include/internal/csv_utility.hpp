#pragma once
#include "common.hpp"
#include "csv_format.hpp"
#include "csv_reader.hpp"
#include "data_type.hpp"
#include "string_view_stream.hpp"

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
        size_t n_cols;                      /**< Number of columns in a CSV */
    };

    /** @name Shorthand Parsing Functions
     *  @brief Convenience functions for parsing small strings
     */
     ///@{
    CSVReader operator ""_csv(const char*, size_t);
    CSVReader operator ""_csv_no_header(const char*, size_t);

    /** Parse CSV from a string view, copying the input into an owned buffer.
     *
     *  Safe for any string_view regardless of the caller's ownership of the
     *  underlying memory.
     */
    CSVReader parse(csv::string_view in, CSVFormat format = CSVFormat::guess_csv());

    /** Parse CSV from an in-memory view with zero copy.
     *
     *  WARNING: Non-owning path. The caller must ensure `in`'s backing memory
     *  remains valid and immutable while the reader may request additional rows
     *  from the source stream.
     *
     *  Already materialized CSVRows remain safe because parsed chunk data is
     *  owned by RawCSVData, so make sure you grab all the CSVRows you need
     *  before the underlying string is destroyed.
     */
    CSVReader parse_unsafe(csv::string_view in, CSVFormat format = CSVFormat::guess_csv());

    CSVReader parse_no_header(csv::string_view in);
    ///@}

    /** @name Utility Functions */
    ///@{
    std::unordered_map<std::string, DataType> csv_data_types(const std::string&);
    CSVFileInfo get_file_info(const std::string& filename);
    int get_col_pos(csv::string_view filename, csv::string_view col_name,
        const CSVFormat& format = CSVFormat::guess_csv());
    ///@}
}