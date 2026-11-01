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

## Empty Fields and Quoting

With minimal quoting, a row containing one empty field is written as `""`
followed by a newline. A row with no fields produces a blank line, and a row
containing two empty fields produces a delimiter followed by a newline. Quoting
the lone empty field preserves that distinction when another CSV reader reads
the output. A custom `DelimWriter` uses its configured quote character.

With `quote_minimal = false`, string fields are always quoted, including empty
strings. Tuple, range, and checked row writes follow the same empty-field rules.

## Checked Row Writes in Both Exception Modes

`try_write_row(error, row)` accepts ordinary containers, tuples, `CSVRow`,
`DataFrameRow`, and `DataFrameColumn` in C++11 and later, with compiler exceptions
enabled or disabled. Row and column proxies preserve visible values and edits;
an invalid proxy or a missing physical field reports a `CSVError`.

@code
csv::CSVError error;
std::ostringstream output;
csv::CSVWriter<std::ostringstream> writer(output);
writer.set_auto_flush(false);
if (!writer.try_write_row(error, row)) {
    // Inspect error before another checked call clears it.
    return;
}
if (!writer.try_flush(error)) {
    // Handle the output failure.
    return;
}
@endcode

A field-access failure discards the current record's buffered prefix and keeps
previously buffered records. I/O failures can leave bytes already written to
the output; they do not promise rollback. Buffered success means accepted into
the writer's buffer. Use `try_flush(error)` or an owned file's `try_close(error)`
to observe final output errors. Borrowed standard streams must have exceptions
disabled when compiler exceptions are disabled. See [No-exceptions and Godot
integration](@ref no_exceptions) for the full error contract.

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

## Writing a Complete DataFrame

`writer << frame` writes the visible column names once, then the visible rows in
order, including sparse edits and structural column changes. It accepts const
frames and is available in C++11 and later. A frame with a schema but no rows
still writes its header. This convenience requires compiler exceptions enabled.
In either exception mode, `writer.try_write_row(error, frame)` writes the same
document and reports errors explicitly.

The writer's delimiter, quoting, and buffering settings apply. With auto-flush
enabled, the underlying stream is flushed once after the document. With
auto-flush disabled, use `writer.flush()` when the output must be available;
the writer also flushes on destruction.

In C++20, `writer.write_rows(frame)` remains a data-row-only operation. It does
not write a header, which is useful when appending to an existing document.
Use `writer.try_write_rows(frame, error)` for the checked equivalent in either
exception mode.

## DataFrame with Sparse Overlay

When working with DataFrames, you can efficiently update specific cells without reconstructing entire rows. The overlay mechanism stores only the changed cells and writes them correctly:

\snippet tests/test_write_csv.cpp DataFrame Sparse Overlay Write Example

## End-to-End Round-Trip Integrity Example

The following test is intentionally write-first then read/verify, but it validates
the same data-integrity guarantee as read-transform-write user workflows.

\snippet tests/test_round_trip.cpp Round Trip Distinct Field Values Example
