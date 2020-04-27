# Dealing with Variable Length CSV Rows

`csv::CSVReader` generally assumes that most rows in a CSV are of the same length.
If your CSV has important data stored in rows which may not be the same length
as the others, then you may want to create your own subclass of CSVReader and
override `bad_row_handler`.

## Examples
 * csv::CSVReader::bad_row_handler
 * csv::internals::CSVGuesser::Guesser::bad_row_handler()