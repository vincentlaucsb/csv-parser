#include <sstream>
#include <vector>
#include <memory>

#include "csv_utility.hpp"
#include "string_view_stream.hpp"

namespace csv {
    /** Shorthand function for parsing an in-memory CSV string.
     *
     *  Copies the input into an owned stringstream, so the caller's backing
     *  memory may be freed immediately after this call returns.
     *
     *  @return A collection of CSVRow objects
     *
     *  @par Example
     *  @snippet tests/test_read_csv.cpp Parse Example
     */
    CSV_INLINE CSVReader parse(csv::string_view in, CSVFormat format) {
        std::unique_ptr<std::istream> ss(new std::stringstream(std::string(in)));
        return CSVReader(std::move(ss), format);
    }

    /** Parse CSV from an in-memory view with zero copy.
     *
     *  Creates a non-owning stream adapter over the provided string_view.
     *  The caller is responsible for keeping backing memory valid and immutable
     *  while CSVReader may request additional rows.
     *
     *  Already materialized CSVRows remain safe because parsed chunk data is
     *  owned by RawCSVData.
     *
     *  @return A collection of CSVRow objects
     */
    CSV_INLINE CSVReader parse_unsafe(csv::string_view in, CSVFormat format) {
        std::unique_ptr<std::istream> stream(new internals::StringViewStream(in));
        return CSVReader(std::move(stream), format);
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
     *  of CSVRow objects.
     *
     *  String literals have static storage duration, so the zero-copy path is
     *  safe here.
     *
     *  @par Example
     *  @snippet tests/test_read_csv.cpp Escaped Comma
     *
     */
    CSV_INLINE CSVReader operator ""_csv(const char* in, size_t n) {
        return parse_unsafe(csv::string_view(in, n));
    }

    /** A shorthand for csv::parse_no_header().
     *
     *  String literals have static storage duration, so the zero-copy path is
     *  safe here.
     */
    CSV_INLINE CSVReader operator ""_csv_no_header(const char* in, size_t n) {
        CSVFormat format;
        format.header_row(-1);
        return parse_unsafe(csv::string_view(in, n), format);
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