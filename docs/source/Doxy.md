# Vince's CSV Library

This is the detailed documentation for Vince's CSV library. 
For quick examples, go to this project's [GitHub page](https://github.com/vincentlaucsb/csv-parser).

## Outline

### CSV Reading
 * csv::CSVFormat: \copybrief csv::CSVFormat
 * csv::CSVReader
  * csv::CSVReader::correct_rows: \copybrief csv::CSVReader::correct_rows
  * csv::CSVReader::utf8_bom: \copybrief csv::CSVReader::utf8_bom
  * csv::CSVReader::get_format(): \copybrief csv::CSVReader::get_format()
  * Manually parsing string fragments
      * csv::CSVReader::feed()
  * Retrieving data
      * csv::CSVReader::read_row()
      * csv::CSVReader::iterator
        * csv::CSVReader::begin()
        * csv::CSVReader::end()
 * csv::parse()
 * csv::operator ""_csv()

 ### See also
 [Dealing with Variable Length CSV Rows](md_docs_source_variable_row_lengths.html)

 ### Working with parsed data
 * csv::CSVRow: \copybrief csv::CSVRow
  * csv::CSVRow::operator std::vector<std::string>()
  * csv::CSVRow::iterator
    * csv::CSVRow::begin()
    * csv::CSVRow::end()
 * csv::CSVField
  * csv::CSVField::get(): \copybrief csv::CSVField::get()
  * csv::CSVField::operator==()

### Statistics
 * csv::CSVStat

### Writing
 * csv::DelimWriter
   * csv::CSVWriter
   * csv::TSVWriter

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
The csv::CSVReader iterators are intended to be used from one thread at a time. However, csv::CSVRow and csv::CSVField objects should be 
thread-safe (since they mainly involve reading data). If you want to perform computations on multiple columns in parallel,
you may want to avoid using the iterators and
use csv::CSVReader::read_row() to manually chunk your data. csv::CSVStat provides an example of how parallel computations
may be performed. (Specifically, look at csv::CSVStat::calc() and csv::CSVStat::calc_worker() in csv_stat.cpp).