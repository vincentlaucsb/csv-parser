@page no_exceptions No-exceptions and Godot integration

# No-exceptions and Godot integration

The explicit-error APIs work with C++11 and later, with compiler exceptions
enabled or disabled. Parsing, DataFrames, serialization, and writing share
`csv::CSVError`; field conversions retain the more specific
`csv::CSVConversionError` cause.

## Build configuration

Exception support is detected automatically from compiler macros. No
library-specific define is required when the consuming build already uses
`-fno-exceptions` with GCC/Clang, or `/EHs-c- /D_HAS_EXCEPTIONS=0` with MSVC.

For a CMake consumer:

```cmake
set(CSV_ENABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
add_subdirectory(csv-parser)
target_link_libraries(my_extension PRIVATE csv)
```

The option applies the exception-disabled flags to the library and its consumers.
The default also respects compiler flags that already disable exceptions.
Use consistent exception and threading settings across all translation units.

For single-header use, generate `csv.hpp` with the `generate_single_header`
target. The checked-in `single_include/csv.hpp` is a compatibility shim.

## Reading native files

Factories return unique ownership and no partially initialized object on error.

```cpp
#include "csv.hpp"
#include <iostream>

void load_items() {
    csv::CSVError error;
    auto reader = csv::CSVReader::try_open("items.csv", error);
    if (!reader) {
        std::cerr << error.message() << '\n';
        return;
    }

    csv::CSVRow row;
    while (reader->try_read_row(row, error)) {
        auto field = row.try_field("damage", error);
        if (error) break;

        int damage = 0;
        csv::CSVConversionError conversion;
        if (!field.try_get(damage, conversion)) {
            std::cerr << csv::csv_conversion_error_message(conversion) << '\n';
            return;
        }
        // Use damage here.
    }
    if (error) std::cerr << error.message() << '\n';
}
```

`try_read_row()` returns `false` with a clear error at EOF. A reader failure is
terminal; further reads report that failure. The same distinction applies to
`try_read_chunk()`, whose output may contain an already consumed prefix on error.

Each checked operation clears stale errors before reporting its own result.
Inspect an error before making another checked call. `CSVError::context` is a
`csv::string_view`: static diagnostics are borrowed, and runtime text is copied
into a private owned buffer. Copies retain valid text independently of the source;
moves clear the source. Use `set_context()` to replace text with an owned copy,
or `from_static()` only for text that remains valid for the program's lifetime.
Success and static diagnostics do not allocate. `message()` formats a string on
demand, including an available OS error. DataFrame
conversion failures report `ConversionFailed` with details in `context`.
Field-conversion APIs report `CSVConversionError` directly.

## Godot resources and exported games

`CSVReader::try_open()` expects a native filesystem path. For Godot's `res://`
and `user://` paths, load the data through Godot's file/resource APIs first, then
pass the resulting UTF-8 bytes to `csv::try_parse()`. Godot's
[FileAccess documentation](https://docs.godotengine.org/en/4.5/classes/class_fileaccess.html)
describes `get_file_as_bytes()`, `get_file_as_string()`, and `get_open_error()`.

Check Godot's loading error before parsing, so an engine I/O failure is handled
at the engine boundary. For raw CSV read with FileAccess in an exported game,
keep the original file available using the import mode or non-resource export
filter described in that documentation (for example, `*.csv`).

Once the engine has supplied the bytes, the parser needs no Godot-specific adapter:

```cpp
bool validate_csv_text(csv::string_view utf8_text, csv::CSVError& error) {
    auto reader = csv::try_parse(utf8_text, error);
    if (!reader) return false;

    csv::CSVRow row;
    while (reader->try_read_row(row, error)) {
        // Read fields with row.try_field() and field.try_get().
    }
    return !error;
}
```

`try_parse()` copies the source into an owned buffer, so the engine's temporary
UTF-8 conversion buffer only needs to survive the call. `try_parse_unsafe()`
avoids that copy; keep its source bytes alive and immutable while unread rows
still depend on them.

For small per-level files, `CSVFormat::threading(false)` selects synchronous
parsing. It also disables speculative parallel parsing for that reader.

## DataFrames and writing

Use `DataFrame<>::try_create()` for reader/file construction. Positional and key
lookups use `try_at()`, `try_find()`, `try_contains()`, and `try_column_view()`.
Rows and columns expose `try_field()`; cells expose `try_get()` and `try_assign()`.
Structural edits use `try_insert_row()`, `try_insert_column()`,
`try_append_column()`, and proxy `try_erase()`.

Default and row-batch DataFrame construction and valid frame-row iteration
remain available. Structural mutation invalidates outstanding proxies as
documented by each operation. Const access continues to produce read-only cells.

Writers expose `try_open()`, `try_write_row(error, ...)`, `try_flush(error)`, and
file-owning `try_close(error)`. Buffered writes are complete only after a
successful explicit flush or close. Destruction performs a best-effort flush;
use the checked completion operations to observe errors.

Reading or writing does not roll back partial progress. A failed write may have
emitted bytes, and a failed batch read may have consumed rows.

## C++23 expected adapters

\cpp23

When the standard library supplies `std::expected`, `CSVField::as<T>()` returns
the same `CSVConversionError` used by diagnostic `try_get()`. The expected-returning
reader factory is a small adapter over the same explicit-error implementation:

```cpp
auto opened = csv::CSVReader::try_open("items.csv");
if (!opened) {
    std::cerr << opened.error().message() << '\n';
    return;
}
auto& reader = **opened;
csv::CSVRow row;
csv::CSVError error;
while (reader.try_read_row(row, error)) { /* process row */ }
if (error) std::cerr << error.message() << '\n';
```

## Error-reporting boundaries

Throwing constructors, `CSVField::get<T>()`, throwing convenience accessors,
and CSVReader iteration are unavailable when exceptions are disabled. Use the
checked operations and explicit row/batch reads. The corresponding convenience
APIs retain their exception categories when exceptions are enabled.

Explicit errors cover library validation and I/O. Allocation failures, resource
exhaustion, and exceptions from user callbacks remain outside this contract.
`SizeLimitExceeded` specifically reports that preserved string-view key bytes
cannot fit in `std::string::max_size()`; it does not catch allocation failures.

Borrowed streams must use non-throwing exception masks in an exception-disabled
build. Checked operations reject incompatible masks without changing the
caller's settings. When exceptions are enabled, source I/O failures are
translated into the shared explicit error model; unrelated user exceptions
retain their existing behavior.
