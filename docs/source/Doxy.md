# Vince's CSV Library

This is the detailed documentation for Vince's CSV library. 
For quick examples, go to this project's [GitHub page](https://github.com/vincentlaucsb/csv-parser).

Python users should start with the [fastpycsv Python bindings](python/index.html).

For exception-disabled builds and engine-owned resources, see
[No-exceptions and Godot integration](@ref no_exceptions).

## Outline

### CSV Reading

 * csv::CSVFormat: \copybrief csv::CSVFormat
   * csv::CSVFormat::chunk_size(): Set chunk size for files with very large rows
 * csv::CSVReader
   * csv::CSVReader::n_rows(): \copybrief csv::CSVReader::n_rows()
   * csv::CSVReader::utf8_bom(): \copybrief csv::CSVReader::utf8_bom()
   * csv::CSVReader::get_format(): \copybrief csv::CSVReader::get_format()
   * Retrieving data
     * csv::CSVReader::iterator: Recommended
       * csv::CSVReader::begin()
       * csv::CSVReader::end()
     * csv::CSVReader::read_row()
     * csv::CSVReader::read_chunk()
 * Convenience Functions
   * csv::parse()
   * csv::operator ""_csv()
   * csv::parse_no_header()
   * csv::operator ""_csv_no_header()
 * File Utilities
   * csv::get_file_info(): Returns row/column counts and detected format for a CSV file
   * csv::get_col_pos(): Returns the zero-based index of a named column

 #### See also
 [CSV Tuning](@ref csv_tuning)

 #### Working with parsed data
 * csv::CSVRow: \copybrief csv::CSVRow
   * csv::CSVRow::operator std::vector<std::string>()
   * csv::CSVRow::iterator
     * csv::CSVRow::begin()
     * csv::CSVRow::end()
   * csv::CSVRow::to_json()
   * csv::CSVRow::to_json_array()
 * csv::CSVField
   * csv::CSVField::get(): \copybrief csv::CSVField::get()
   * csv::CSVField::operator==()

 #### See also
 [Scalar Conversion Reference](@ref scalar_conversions)

### DataFrame

An in-memory table built from a csv::CSVReader or a batch of csv::CSVRow values.
Frames support positional access, optional keyed lookup, column extraction,
editing, and grouping.

 * csv::DataFrame: Main container class (template parameter is the key type, default `std::string`)
   * Construction
     * From csv::CSVReader or a row batch for an unkeyed frame
     * From csv::CSVReader with an explicit key column for a keyed frame
     * csv::DataFrame::try_create(): Checked reader/file construction in either exception mode
   * Inspection
     * csv::DataFrame::n_rows()
     * csv::DataFrame::n_cols()
     * csv::DataFrame::size()
     * csv::DataFrame::empty()
     * csv::DataFrame::has_column()
     * csv::DataFrame::columns()
   * Row access by key
     * csv::DataFrame::operator[](): Access by key value (O(1))
     * csv::DataFrame::contains(): Check if a key exists
     * csv::DataFrame::try_find(): Checked keyed lookup
     * csv::DataFrame::try_contains(): Checked key membership
   * Row access by position
     * csv::DataFrame::at(): Positional access by index (use instead of `operator[](size_t)` for integer-keyed DataFrames)
     * csv::DataFrame::try_at(): Checked positional lookup
   * Column extraction
     * csv::DataFrame::column(): Extract all values from a named column as `std::vector<T>`
     * csv::DataFrame::try_column_view(): Checked column proxy access
   * Editing
     * csv::DataFrameRow::try_field(): Get a checked cell proxy
     * csv::DataFrameCell::try_assign(): Assign a cell value
     * csv::DataFrameRow::try_erase(): Remove the row represented by a mutable proxy
     * csv::DataFrameColumn::try_erase(): Remove the column represented by a mutable proxy
   * Grouping
     * csv::DataFrame::group_by(): Group row indices by an arbitrary key function or column name
   * Iteration
     * csv::DataFrame::begin()
     * csv::DataFrame::end()
 * csv::DataFrameRow: Proxy row object returned by DataFrame access methods
   * csv::DataFrameRow::operator[](): Access a field by column name
   * csv::DataFrameRow::size()
   * csv::DataFrameRow::empty()
   * csv::DataFrameRow::get_col_names()
 * csv::DataFrameOptions: Configuration for DataFrame construction
   * Duplicate key policy (`THROW`, `OVERWRITE`, or `KEEP_FIRST`)
   * `set_key_column()`: Specify which column to use as the key
   * `set_throw_on_missing_key()`: Control exception behavior for missing keys

### ETL Utilities
 * csv::csv_data_types(): Infer SQL-friendly column data types from a CSVReader or any supported CSVReader constructor input
 * csv::chunk_parallel_apply(): Chunked parallel column processing over a CSVReader

 #### See also
 [High Performance ETL](@ref high_performance_etl)

### Deep Dives

Implementation notes and internal architecture documents are collected under
[Internal / Deep Dives](@ref internal_deep_dives).

### CSV Writing
The [CSV Writing Guide](@ref csv_writing_guide) contains a 
high-level overview of writing CSVs.

 * csv::make_csv_writer(): Construct a csv::CSVWriter
 * csv::make_tsv_writer(): Construct a csv::TSVWriter
 * csv::DelimWriter
   * Pre-Defined Specializations
     * csv::CSVWriter
     * csv::TSVWriter
   * Methods
     * csv::DelimWriter::operator<<()
     * csv::DelimWriter::write_row()
     * csv::DelimWriter::write_rows(): C++20 data-row batches
     * csv::DelimWriter::try_open(): Open an owned output file
     * csv::DelimWriter::try_write_row(): Checked rows or a complete DataFrame, in either exception mode
     * csv::DelimWriter::try_write_rows(): Checked C++20 data-row batches
     * csv::DelimWriter::try_flush()
     * csv::DelimWriter::try_close(): Finish an owned output file

## Frequently Asked Questions

### How does automatic starting row detection work?
See "How does automatic delimiter detection work?"

### How does automatic delimiter detection work?
For each candidate delimiter, the reader groups sampled rows by field count
and scores each group as its field count multiplied by its row count. The
delimiter with the highest group score wins. Header detection uses the first
row when it has at least the winning group's field count; otherwise it uses
the first row in that group. Configure csv::CSVFormat explicitly when the
format is known.

### Is the CSV parser thread-safe?
This library already does a lot of work behind the scenes to use threads to squeeze
performance from your CPU. However, ambitious users who are in the mood for
experimenting should follow these guidelines:
 * Treat each csv::CSVReader as a single-consumer object. Do not concurrently call
   row-consumption APIs, iterate, mutate, or destroy the same reader from user threads.
 * A reader's own background worker may still overlap an ordinary single-threaded
   caller after rows have been queued. csv::CSVReader::eof() is synchronized with
   that worker, and csv::CSVReader::speculative_diagnostics() returns a coherent
   snapshot rather than counters from a partially completed merge.
 * That metadata guarantee is narrow: it does not make other operations on a shared
   csv::CSVReader generally thread-safe.
 * csv::CSVReader::iterator should only be used from one thread
   * A workaround is to chunk blocks of `CSVRow` objects together and 
     create separate threads to process each column
 * csv::CSVRow may be safely processed from multiple threads
 * csv::CSVField objects should only be read from one thread at a time
   * **Note**: csv::CSVRow::operator[]() produces separate copies of `csv::CSVField` objects
