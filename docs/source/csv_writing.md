@page csv_writing_guide CSV Writing Guide

# CSV Writing Guide

This page summarizes write-side APIs and practical usage patterns for emitting
CSV/TSV data.

## Core Writer APIs

* `csv::make_csv_writer()`
* `csv::make_tsv_writer()`
* `csv::DelimWriter`

Use `csv::make_csv_writer()` for comma-delimited output and
`csv::make_tsv_writer()` for tab-delimited output.

If you want buffered behavior, call `set_auto_flush(false)` on the writer
instead of using a separate factory:

@code
std::stringstream output;
auto writer = csv::make_csv_writer(output).set_auto_flush(false);
@endcode

## Writing Containers with `operator<<`

Any row-like container of string-convertible values can be streamed directly.

\snippet tests/test_write_csv.cpp CSV Writer Example

### Writing Tuples and Custom Types

`DelimWriter` can also serialize tuples and custom types that provide a string
conversion.

\snippet tests/test_write_csv.cpp CSV Writer Tuple Example

## Using `write_row()`

The `write_row()` method can be used to write rows with arbitrary fields and mixed types without having to construct a container first.

Through the magic of SFINAE, `write_row()` also supports any of the operations of `operator<<`.

\snippet tests/test_write_csv.cpp CSV write_row Variadic Example

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

### C++20 Ranges Version

With C++20, you can use `std::ranges::views` to elegantly reorder fields in a single expression:

\snippet tests/test_write_csv.cpp CSV Ranges Reordering Example

## DataFrame with Sparse Overlay

When working with DataFrames, you can efficiently update specific cells without reconstructing entire rows. The overlay mechanism stores only the changed cells and writes them correctly:

\snippet tests/test_write_csv.cpp DataFrame Sparse Overlay Write Example

## End-to-End Round-Trip Integrity Example

The following test is intentionally write-first then read/verify, but it validates
the same data-integrity guarantee as read-transform-write user workflows.

\snippet tests/test_round_trip.cpp Round Trip Distinct Field Values Example
