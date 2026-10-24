#pragma once
#include "common.hpp"
#include "csv_exceptions.hpp"
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
#include <utility>

namespace csv {
    /** Returned by the file inspection utilities. */
    struct CSVFileInfo {
        std::string filename;               /**< Filename */
        std::vector<std::string> col_names; /**< CSV column names */
        char delim;                         /**< Delimiting character */
        size_t n_rows;                      /**< Number of rows in a file */
        size_t n_cols;                      /**< Number of columns in a CSV */
        size_t parse_worker_count;          /**< Number of parser worker threads used */
        internals::SpeculativeParseDiagnostics speculative_diagnostics;
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
    inline std::unique_ptr<CSVReader> try_parse(
        csv::string_view in, CSVError& error, const CSVFormat& format = CSVFormat::guess_csv()
    ) {
        std::unique_ptr<std::istream> stream(new std::stringstream(std::string(in)));
        return CSVReader::try_open(std::move(stream), error, format);
    }

#if CSV_EXCEPTIONS_ENABLED
    inline CSVReader parse(csv::string_view in, const CSVFormat& format = CSVFormat::guess_csv()) {
        CSVError error;
        auto reader = try_parse(in, error, format);
        internals::throw_error(error);
        return std::move(*reader);
    }
#endif

    /** Parse CSV from an in-memory view with zero copy.
     *
     *  WARNING: Non-owning path. The caller must ensure `in`'s backing memory
     *  remains valid and immutable while the reader may request additional rows
     *  from the source stream.
     *
     *  Rows already obtained from the reader remain valid, but unread rows
     *  still depend on the source view staying alive.
     */
    inline std::unique_ptr<CSVReader> try_parse_unsafe(
        csv::string_view in, CSVError& error, const CSVFormat& format = CSVFormat::guess_csv()
    ) {
        std::unique_ptr<std::istream> stream(new internals::StringViewStream(in));
        return CSVReader::try_open(std::move(stream), error, format);
    }

#if CSV_EXCEPTIONS_ENABLED
    inline CSVReader parse_unsafe(csv::string_view in, const CSVFormat& format = CSVFormat::guess_csv()) {
        CSVError error;
        auto reader = try_parse_unsafe(in, error, format);
        internals::throw_error(error);
        return std::move(*reader);
    }
#endif

#if CSV_EXCEPTIONS_ENABLED
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
#endif
    ///@}

    /** @name Utility Functions */
    ///@{
    /** Infer SQL-friendly column data types from an existing CSVReader.
     *
     *  This consumes rows from `reader` using the chunked ETL path and returns
     *  one inferred `DataType` per column name.
     */
    std::unordered_map<std::string, DataType> try_csv_data_types(CSVReader& reader, CSVError& error);

    /** Infer types from a filename, borrowed stream, or owned stream, reporting I/O errors. */
    template<typename Source>
    inline auto try_csv_data_types(
        Source&& source, CSVError& error, const CSVFormat& format = CSVFormat::guess_csv()
    ) -> decltype(CSVReader::try_open(std::forward<Source>(source), error, format),
        std::unordered_map<std::string, DataType>()) {
        auto reader = CSVReader::try_open(std::forward<Source>(source), error, format);
        return reader ? try_csv_data_types(*reader, error) : std::unordered_map<std::string, DataType>();
    }

#if CSV_EXCEPTIONS_ENABLED
    std::unordered_map<std::string, DataType> csv_data_types(CSVReader& reader);

    /** Infer SQL-friendly column data types from any CSVReader constructor input.
     *
     *  This convenience overload forwards its arguments directly to
     *  `CSVReader`, so it supports filenames, `std::istream` sources, owned
     *  streams, and custom `CSVFormat` combinations without additional wrapper
     *  code.
     *
     *  @par Example
     *  @code
     *  std::istringstream input("name,age\nAlice,30\nBob,41\n");
     *  CSVFormat format;
     *  format.delimiter(',').header_row(0);
     *
     *  auto dtypes = csv::csv_data_types(input, format);
     *  @endcode
     */
    template<
        typename... ReaderArgs,
        csv::enable_if_t<std::is_constructible<CSVReader, ReaderArgs...>::value, int> = 0
    >
    inline std::unordered_map<std::string, DataType> csv_data_types(ReaderArgs&&... reader_args) {
        CSVReader reader(std::forward<ReaderArgs>(reader_args)...);
        return csv_data_types(reader);
    }
#endif

    namespace internals {
        // Shared batch traversal lets callers inspect joined task errors before
        // consuming another batch, including when the source never reaches EOF.
        template<typename Fn>
        inline bool try_for_each_csv_chunk(
            CSVReader& reader, Fn&& fn, CSVError& error, size_t chunk_size) {
            error.clear();
            if (chunk_size == 0)
                return fail(error, CSVErrorCode::InvalidArgument, ERROR_CHUNK_PARALLEL_APPLY_ZERO);
            std::vector<CSVRow> rows;
            while (reader.try_read_chunk(rows, chunk_size, error)) {
                DataFrame<> batch(std::move(rows));
                if (!fn(batch, error)) return false;
            }
            return !error;
        }
    }

    /** Apply a per-column batch function over a CSVReader using a reusable executor.
     *
     *  Reads the source in chunks, promotes each chunk into a temporary DataFrame,
     *  and applies `fn(column, states[column.index()])`.
     *
     *  Callbacks may treat each batch DataFrame as read-mostly, and sparse
     *  overlay cell edits are synchronized at row granularity. If you need more
     *  involved batch orchestration, use `CSVReader::read_chunk()` and construct
     *  a batch-scoped `DataFrame` yourself.
     *
     *  @throws std::invalid_argument if `chunk_size == 0`
     */
    template<typename State, typename Fn>
    inline bool try_chunk_parallel_apply(
        CSVReader& reader,
        DataFrameExecutor& executor,
        std::vector<State>& states,
        Fn&& fn,
        CSVError& error,
        size_t chunk_size = 50000
    ) {
        return internals::try_for_each_csv_chunk(reader,
            [&executor, &states, &fn](DataFrame<>& batch, CSVError& failure) {
                return batch.try_column_parallel_apply(executor, states, std::forward<Fn>(fn), failure);
            }, error, chunk_size);
    }

#if CSV_EXCEPTIONS_ENABLED
    template<typename State, typename Fn>
    inline void chunk_parallel_apply(
        CSVReader& reader,
        DataFrameExecutor& executor,
        std::vector<State>& states,
        Fn&& fn,
        size_t chunk_size = 50000
    ) {
        CSVError error;
        try_chunk_parallel_apply(reader, executor, states, std::forward<Fn>(fn), error, chunk_size);
        internals::throw_dataframe_error(error);
    }
#endif

    /** Apply a per-column batch function over a CSVReader with a temporary executor.
     *
     *  This is the convenience overload for the common case where callers do not
     *  need to reuse worker threads across multiple reader pipelines.
     */
    template<typename State, typename Fn>
    inline bool try_chunk_parallel_apply(
        CSVReader& reader,
        std::vector<State>& states,
        Fn&& fn,
        CSVError& error,
        size_t chunk_size = 50000
    ) {
        DataFrameExecutor executor;
        return try_chunk_parallel_apply(reader, executor, states, std::forward<Fn>(fn), error, chunk_size);
    }

#if CSV_EXCEPTIONS_ENABLED
    template<typename State, typename Fn>
    inline void chunk_parallel_apply(
        CSVReader& reader,
        std::vector<State>& states,
        Fn&& fn,
        size_t chunk_size = 50000
    ) {
        CSVError error;
        try_chunk_parallel_apply(reader, states, std::forward<Fn>(fn), error, chunk_size);
        internals::throw_dataframe_error(error);
    }
#endif

    /** Get basic information about a CSV file
     *  @include programs/csv_info.cpp
     */
    inline CSVFileInfo try_get_file_info(const std::string& filename, CSVError& error) {
        auto reader = CSVReader::try_open(filename, error);
        if (!reader) return {};

        CSVFormat format = reader->get_format();
        char delimiter = 0;
        if (!format.try_get_delim(delimiter, error)) return {};

        std::vector<CSVRow> rows;
        while (reader->try_read_chunk(rows, 50000, error)) {}
        if (error) return {};

        return {
            filename,
            reader->get_col_names(),
            delimiter,
            reader->n_rows(),
            reader->get_col_names().size(),
            reader->parse_worker_count(),
            reader->speculative_diagnostics()
        };
    }

#if CSV_EXCEPTIONS_ENABLED
    inline CSVFileInfo get_file_info(const std::string& filename) {
        CSVError error;
        auto result = try_get_file_info(filename, error);
        internals::throw_error(error);
        return result;
    }
#endif

    /** Get the column names of a CSV file using just the first 500KB. */
    inline std::vector<std::string> try_get_col_names(
        csv::string_view filename, CSVError& error,
        const CSVFormat& format = CSVFormat::guess_csv()
    ) {
        auto head = internals::parser::get_csv_head(filename, error);
        if (error) return {};
        auto reader = try_parse_unsafe(head, error, format);
        return reader ? reader->get_col_names() : std::vector<std::string>();
    }

    /** Find a column position, returning CSV_NOT_FOUND for an absent name or on failure.
     *  An absent name is not an error; inspect error to distinguish an I/O failure.
     */
    inline long long try_get_col_pos(
        csv::string_view filename, csv::string_view col_name, CSVError& error,
        const CSVFormat& format = CSVFormat::guess_csv()
    ) {
        auto col_names = try_get_col_names(filename, error, format);
        if (error) return CSV_NOT_FOUND;
        const auto position = std::find(col_names.begin(), col_names.end(), col_name);
        return position == col_names.end() ? CSV_NOT_FOUND
            : static_cast<long long>(std::distance(col_names.begin(), position));
    }

#if CSV_EXCEPTIONS_ENABLED
    /** Get the column names of a CSV file using just the first 500KB. */
    inline std::vector<std::string> get_col_names(
        csv::string_view filename,
        const CSVFormat& format = CSVFormat::guess_csv()
    ) {
        CSVError error;
        auto result = try_get_col_names(filename, error, format);
        internals::throw_error(error);
        return result;
    }

    /** Find the position of a column in a CSV file or CSV_NOT_FOUND otherwise. */
    inline long long get_col_pos(
        csv::string_view filename, csv::string_view col_name,
        const CSVFormat& format = CSVFormat::guess_csv()
    ) {
        CSVError error;
        const auto result = try_get_col_pos(filename, col_name, error, format);
        internals::throw_error(error);
        return result;
    }
#endif
    ///@}
}
