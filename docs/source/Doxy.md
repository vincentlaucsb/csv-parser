# Vince's CSV Library

This is the detailed documentation for Vince's CSV library. 
For quick examples, go to this project's [GitHub page](https://github.com/vincentlaucsb/csv-parser).

## Outline

### CSV Reading
 * csv::CSVFormat: \copybrief csv::CSVFormat
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

### Statistics
 * csv::CSVStat

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
First, the CSV reader attempts to parse the first 100 lines of a CSV file as if the delimiter were a pipe, tab, comma, etc.
Out of all the possible delimiter choices, the delimiter which produces the highest number of `rows * columns` (where all rows
are of a consistent length) is chosen as the winner.

However, if the CSV file has leading comments, or has less than 100 lines, a second heuristic will be used. The CSV reader again
parses the first 100 lines using each candidate delimiter, but tallies up the length of each row parsed. Then, the delimiter with
the largest most common row length `n` is chosen as the winner, and the line number where the first row of length `n` occurs
is chosen as the starting row.

Because you can subclass csv::CSVReader, you can implement your own guessing hueristic. csv::internals::CSVGuesser may be used as a helpful guide in doing so.

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