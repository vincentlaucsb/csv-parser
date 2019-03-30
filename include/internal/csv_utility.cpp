#include <vector>

#include "constants.hpp"
#include "csv_utility.hpp"
#include "csv_reader.hpp"

namespace csv {
    /**
     *  @brief Shorthand function for parsing an in-memory CSV string,
     *  a collection of CSVRow objects
     *
     *  \snippet tests/test_read_csv.cpp Parse Example
     *
     */
    CSVCollection parse(const std::string& in, CSVFormat format) {
        CSVReader parser(format);
        parser.feed(in);
        parser.end_feed();
        return parser.records;
    }

    /**
     * @brief Parse a RFC 4180 CSV string, returning a collection
     *        of CSVRow objects
     *
     * **Example:**
     *  \snippet tests/test_read_csv.cpp Escaped Comma
     *
     */
    CSVCollection operator ""_csv(const char* in, size_t n) {
        std::string temp(in, n);
        return parse(temp);
    }

    /**
     *  @brief Return a CSV's column names
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     */
    std::vector<std::string> get_col_names(const std::string& filename, CSVFormat format) {
        CSVReader reader(filename, format);
        return reader.get_col_names();
    }

    /**
     *  @brief Find the position of a column in a CSV file or CSV_NOT_FOUND otherwise
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] col_name  Column whose position we should resolve
     *  @param[in] format    Format of the CSV file
     */
    int get_col_pos(
        const std::string filename,
        const std::string col_name,
        const CSVFormat format) {
        CSVReader reader(filename, format);
        return reader.index_of(col_name);
    }

    /** @brief Get basic information about a CSV file
     *  \include programs/csv_info.cpp
     */
    CSVFileInfo get_file_info(const std::string& filename) {
        CSVReader reader(filename);
        CSVFormat format = reader.get_format();
        for (auto& row : reader) {
            #ifndef NDEBUG
            SUPPRESS_UNUSED_WARNING(row);
            #endif
        }

        CSVFileInfo info = {
            filename,
            reader.get_col_names(),
            format.delim,
            reader.correct_rows,
            (int)reader.get_col_names().size()
        };

        return info;
    }
}