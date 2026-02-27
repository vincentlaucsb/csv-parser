# Vince's CSV Library

This is the detailed documentation for Vince's CSV library. 
For quick examples, go to this project's [GitHub page](https://github.com/vincentlaucsb/csv-parser).

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
 * Convenience Functions
   * csv::parse()
   * csv::operator ""_csv()
   * csv::parse_no_header()
   * csv::operator ""_csv_no_header()
 * File Utilities
   * csv::get_file_info(): Returns row/column counts and detected format for a CSV file
   * csv::get_col_pos(): Returns the zero-based index of a named column

 #### See also
 [Dealing with Variable Length CSV Rows](md_docs_source_variable_row_lengths.html)

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

### DataFrame

An in-memory keyed table built from a csv::CSVReader. Supports O(1) key lookup,
column extraction, editing, and grouping.

 * csv::DataFrame: Main container class (template parameter is the key type, default `std::string`)
   * Construction
     * From csv::CSVReader with an explicit key column
   * Inspection
     * csv::DataFrame::n_rows()
     * csv::DataFrame::n_cols()
     * csv::DataFrame::size()
     * csv::DataFrame::empty()
     * csv::DataFrame::has_column()
     * csv::DataFrame::get_col_names()
   * Row access by key
     * csv::DataFrame::operator[](): Access by key value (O(1))
     * csv::DataFrame::contains(): Check if a key exists
     * csv::DataFrame::try_get(): Non-throwing keyed lookup
   * Row access by position
     * csv::DataFrame::iloc(): Positional access by index (use instead of `operator[](size_t)` for integer-keyed DataFrames)
     * csv::DataFrame::try_get(): Non-throwing positional lookup (overloaded on `size_t`)
   * Column extraction
     * csv::DataFrame::column(): Extract all values from a named column as `std::vector<T>`
   * Editing
     * csv::DataFrame::set(): Edit a cell by key and column name
     * csv::DataFrame::set_at(): Edit a cell by position and column name
     * csv::DataFrame::erase_row(): Remove a row by key
     * csv::DataFrame::erase_row_at(): Remove a row by position
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
   * Duplicate key policy (`OVERWRITE` or `KEEP_FIRST`)
   * `set_key_column()`: Specify which column to use as the key
   * `set_throw_on_missing_key()`: Control exception behavior for missing keys

### Statistics
 * csv::CSVStat
   * csv::CSVStat::get_mean(): Per-column means
   * csv::CSVStat::get_variance(): Per-column variances
   * csv::CSVStat::get_mins(): Per-column minimums
   * csv::CSVStat::get_maxes(): Per-column maximums
   * csv::CSVStat::get_counts(): Per-column value frequency counts
   * csv::CSVStat::get_dtypes(): Per-column inferred data types
   * csv::CSVStat::get_col_names()

### CSV Writing
 * csv::make_csv_writer(): Construct a csv::CSVWriter
 * csv::make_tsv_writer(): Construct a csv::TSVWriter
 * csv::DelimWriter
   * Pre-Defined Specializations
     * csv::CSVWriter
     * csv::TSVWriter
   * Methods
     * csv::DelimWriter::operator<<()

## Frequently Asked Questions

### How does automatic starting row detection work?
See "How does automatic delimiter detection work?"

### How does automatic delimiter detection work?
See the implementation in csv::internals::_guess_format() — the source is the authoritative reference and is kept up to date.

### Is the CSV parser thread-safe?
This library already does a lot of work behind the scenes to use threads to squeeze
performance from your CPU. However, ambitious users who are in the mood for
experimenting should follow these guidelines:
 * csv::CSVReader::iterator should only be used from one thread
   * A workaround is to chunk blocks of `CSVRow` objects together and 
     create separate threads to process each column
 * csv::CSVRow may be safely processed from multiple threads
 * csv::CSVField objects should only be read from one thread at a time
   * **Note**: csv::CSVRow::operator[]() produces separate copies of `csv::CSVField` objects