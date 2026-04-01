# CSV Writing Guide

This page summarizes write-side APIs and practical usage patterns for emitting
CSV/TSV data.

## Core Writer APIs

* `csv::make_csv_writer()`
* `csv::make_tsv_writer()`
* `csv::DelimWriter`

Use `csv::make_csv_writer()` for comma-delimited output and
`csv::make_tsv_writer()` for tab-delimited output.

## Writing Containers with `operator<<`

Any row-like container of string-convertible values can be streamed directly.

\snippet tests/test_write_csv.cpp CSV Writer Example

## Writing Tuples and Custom Types

`DelimWriter` can also serialize tuples and custom types that provide a string
conversion.

\snippet tests/test_write_csv.cpp CSV Writer Tuple Example

## Data Reordering Workflow

For read-transform-write pipelines, `csv::CSVRow` supports conversion to
`std::vector<std::string>`, which makes it straightforward to reorder/select
fields before writing.

Typical flow:

1. Read with `CSVReader`
2. Convert row to `std::vector<std::string>`
3. Reorder/select fields
4. Emit with `CSVWriter`

\snippet tests/test_write_csv.cpp CSV Reordering Example
