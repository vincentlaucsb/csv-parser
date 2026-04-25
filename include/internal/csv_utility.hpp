#pragma once
#include "common.hpp"
#include "csv_format.hpp"
#include "csv_reader.hpp"
#include "data_frame.hpp"
#include "data_type.hpp"
#include "string_view_stream.hpp"

#include <memory>
#include <sstream>
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

    /** Parse CSV from a string view, copying the input into an owned buffer.
     *
     *  Safe for any string_view regardless of the caller's ownership of the
     *  underlying memory.
     *
     *  @par Example
     *  @snippet tests/test_read_csv.cpp Parse Example
     */
    inline CSVReader parse(csv::string_view in, const CSVFormat& format = CSVFormat::guess_csv()) {
        std::unique_ptr<std::istream> ss(new std::stringstream(std::string(in)));
        return CSVReader(std::move(ss), format);
    }

    /** Parse CSV from an in-memory view with zero copy.
     *
     *  WARNING: Non-owning path. The caller must ensure `in`'s backing memory
     *  remains valid and immutable while the reader may request additional rows
     *  from the source stream.
     *
     *  Rows already obtained from the reader remain valid, but unread rows
     *  still depend on the source view staying alive.
     */
    inline CSVReader parse_unsafe(csv::string_view in, CSVFormat format = CSVFormat::guess_csv()) {
        std::unique_ptr<std::istream> stream(new internals::StringViewStream(in));
        return CSVReader(std::move(stream), format);
    }

    /** Parses a CSV string with no headers. */
    inline CSVReader parse_no_header(csv::string_view in) {
        CSVFormat format;
        format.header_row(-1);
        return parse(in, format);
    }

    /** Parse a RFC 4180 CSV string.
     *
     *  String literals have static storage duration, so the zero-copy path is
     *  safe here.
     *
     *  @par Example
     *  @snippet tests/test_read_csv.cpp Escaped Comma
     */
    inline CSVReader operator ""_csv(const char* in, size_t n) {
        return parse_unsafe(csv::string_view(in, n));
    }

    /** A shorthand for csv::parse_no_header().
     *
     *  String literals have static storage duration, so the zero-copy path is
     *  safe here.
     */
    inline CSVReader operator ""_csv_no_header(const char* in, size_t n) {
        CSVFormat format;
        format.header_row(-1);
        return parse_unsafe(csv::string_view(in, n), format);
    }
    ///@}

    /** @name Utility Functions */
    ///@{
    std::unordered_map<std::string, DataType> csv_data_types(const std::string&);

    /** Apply a per-column batch function over a CSVReader using a reusable executor.
     *
     *  Reads the source in chunks, promotes each chunk into a temporary DataFrame,
     *  and applies `fn(column, states[column.index()])`.
     *
     *  @throws std::invalid_argument if `chunk_size == 0`
     */
    template<typename State, typename Fn>
    inline void chunk_parallel_apply(
        CSVReader& reader,
        DataFrameExecutor& executor,
        std::vector<State>& states,
        Fn&& fn,
        size_t chunk_size = 50000
    ) {
        if (chunk_size == 0) {
            throw std::invalid_argument("chunk_parallel_apply() requires a non-zero chunk size.");
        }

        std::vector<CSVRow> rows;
        DataFrame<> batch;

        while (reader.read_chunk(rows, chunk_size)) {
            if (batch.empty()) {
                batch = DataFrame<>(std::move(rows));
            } else {
                batch.swap_rows(rows);
            }

            batch.column_parallel_apply(executor, states, std::forward<Fn>(fn));
        }
    }

    /** Apply a per-column batch function over a CSVReader with a temporary executor.
     *
     *  This is the convenience overload for the common case where callers do not
     *  need to reuse worker threads across multiple reader pipelines.
     */
    template<typename State, typename Fn>
    inline void chunk_parallel_apply(
        CSVReader& reader,
        std::vector<State>& states,
        Fn&& fn,
        size_t chunk_size = 50000
    ) {
        DataFrameExecutor executor;
        chunk_parallel_apply(reader, executor, states, std::forward<Fn>(fn), chunk_size);
    }

    /** Get basic information about a CSV file
     *  @include programs/csv_info.cpp
     */
    inline CSVFileInfo get_file_info(const std::string& filename) {
        CSVReader reader(filename);
        CSVFormat format = reader.get_format();
        for (auto it = reader.begin(); it != reader.end(); ++it);

        return {
            filename,
            reader.get_col_names(),
            format.get_delim(),
            reader.n_rows(),
            reader.get_col_names().size()
        };
    }

    /** Get the column names of a CSV file using just the first 500KB. */
    inline std::vector<std::string> get_col_names(
        csv::string_view filename,
        const CSVFormat& format = CSVFormat::guess_csv()) {
        auto head = internals::get_csv_head(filename);
        return parse_unsafe(head, format).get_col_names();
    }

    /** Find the position of a column in a CSV file or CSV_NOT_FOUND otherwise. */
    inline long long get_col_pos(csv::string_view filename, csv::string_view col_name,
        const CSVFormat& format = CSVFormat::guess_csv()) {
        auto col_names = get_col_names(filename, format);
        return col_names.empty() ? CSV_NOT_FOUND :
            std::distance(col_names.begin(), std::find(col_names.begin(), col_names.end(), col_name));
    }
    ///@}
}
