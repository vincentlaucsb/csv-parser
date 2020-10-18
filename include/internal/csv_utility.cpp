#include <sstream>
#include <vector>

#include "csv_utility.hpp"

namespace csv {
    /** Shorthand function for parsing an in-memory CSV string
     *
     *  @return A collection of CSVRow objects
     *
     *  @par Example
     *  @snippet tests/test_read_csv.cpp Parse Example
     */
    CSV_INLINE CSVReader parse(csv::string_view in, CSVFormat format) {
        std::stringstream stream(in.data());
        return CSVReader(stream, format);
    }

    /** Parses a CSV string with no headers
     *
     *  @return A collection of CSVRow objects
     */
    CSV_INLINE CSVReader parse_no_header(csv::string_view in) {
        CSVFormat format;
        format.header_row(-1);

        return parse(in, format);
    }

    /** Parse a RFC 4180 CSV string, returning a collection
     *  of CSVRow objects
     *
     *  @par Example
     *  @snippet tests/test_read_csv.cpp Escaped Comma
     *
     */
    CSV_INLINE CSVReader operator ""_csv(const char* in, size_t n) {
        return parse(csv::string_view(in, n));
    }

    /** A shorthand for csv::parse_no_header() */
    CSV_INLINE CSVReader operator ""_csv_no_header(const char* in, size_t n) {
        return parse_no_header(csv::string_view(in, n));
    }

    /**
     *  Find the position of a column in a CSV file or CSV_NOT_FOUND otherwise
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] col_name  Column whose position we should resolve
     *  @param[in] format    Format of the CSV file
     */
    CSV_INLINE int get_col_pos(
        csv::string_view filename,
        csv::string_view col_name,
        const CSVFormat& format) {
        CSVReader reader(filename, format);
        return reader.index_of(col_name);
    }

    /** Get basic information about a CSV file
     *  @include programs/csv_info.cpp
     */
    CSV_INLINE CSVFileInfo get_file_info(const std::string& filename) {
        CSVReader reader(filename);
        CSVFormat format = reader.get_format();
        for (auto it = reader.begin(); it != reader.end(); ++it);

        CSVFileInfo info = {
            filename,
            reader.get_col_names(),
            format.get_delim(),
            reader.n_rows(),
            reader.get_col_names().size()
        };

        return info;
    }
}