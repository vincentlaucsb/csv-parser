## Test Architecture

### Framework
- **Catch2 v3.6.0**: Modern C++ testing framework with SECTION support for testing multiple code paths

### Shared Test Utilities (`tests/shared/`)

**Always check `tests/shared/` before implementing any test helper from scratch.**

| File | Purpose |
|------|---------|
| `shared/file_guard.hpp` | RAII temp-file cleanup — **use this for every temp file** |
| `shared/float_test_cases.hpp` | Shared floating-point edge-case data |

#### FileGuard — RAII temp file cleanup

> **AI helpers**: the class is called `FileGuard`, not `TempFile`, `ScopedFile`, or `TempCSVFile`.
> Include it as `#include "shared/file_guard.hpp"`.  Never use raw `std::remove()`.

```cpp
#include "shared/file_guard.hpp"

TEST_CASE("My test") {
    FileGuard cleanup("./tests/data/tmp_foo.csv");  // deleted on scope exit
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "A,B\n1,2\n";
    }
    CSVReader reader(cleanup.filename);   // mmap path
    REQUIRE(...);
}   // std::remove() called here even if REQUIRE throws
```

---

### Test Organization

#### Path Testing Pattern
Most tests validate both code paths using a shared validation lambda:
```cpp
auto validate_reader = [&](CSVReader& reader) {
    // Common validation logic for both paths
};

SECTION("Memory-mapped file path") {
    CSVReader reader(filename);
    validate_reader(reader);
}

SECTION("std::istream path") {
    std::ifstream infile(filename);
    CSVReader reader(infile, CSVFormat());
    validate_reader(reader);
}
```

This pattern catches path-specific bugs like issue #281 (stream-only parsing error).

#### File Cleanup
Tests use RAII cleanup via [FileGuard](shared/file_guard.hpp) — see the **Shared Test Utilities** section above for full usage.

### Test Files

> **Rule**: Every `test_*.cpp` file in `tests/` **must** appear in `target_sources()` in `tests/CMakeLists.txt`.
> Files not listed there are silently never compiled or run.
> When adding a new test file, add it to CMakeLists.txt in the same commit.
> When asked to audit this, compare `ls tests/test_*.cpp` against the `target_sources()` list.

- **test_error_handling.cpp**: Exception propagation from PR #282
  - Validates worker thread exceptions reach main thread
  - Tests chunk boundary corruption detection
  
- **test_round_trip.cpp**: Write/read integrity across 10MB boundaries
  - Basic functionality → distinct values → quoted edge cases
  - Tests both mmap and stream parsing paths
  
- **test_csv_format.cpp**: CSV format detection and configuration
  - Issue #285: no_header() preservation with delimiter guessing
  
- **test_guess_csv.cpp**: Delimiter and header detection heuristics
  - Issue #283: header detection with wide headers
  
- **test_read_csv_file.cpp**: File reading and column access
  - get_col_pos(), prevent column name overwriting
  
- **test_write_csv.cpp**: CSV writing and numeric conversion
  - Buffered vs non-buffered writing modes
  
- **test_csv_row.cpp**, **test_csv_field.cpp**: Individual component tests
  
- **test_csv_iterator.cpp**: Iterator functionality and edge cases
  
- **test_csv_ranges.cpp**: Range-based for loop support
  
- **test_csv_row_json.cpp**: JSON export functionality

### Key Patterns

1. **Validation Lambdas**: Write once, test both paths
2. **SECTION Grouping**: Organize related scenarios
3. **FileGuard RAII**: Guaranteed cleanup for temp files
4. **Distinct Values**: Detect cross-field corruption
5. **Chunk Boundary Testing**: Cross 10MB ITERATION_CHUNK_SIZE

### Data Files
Test data in `tests/data/` is a git submodule:
- `fake_data/`: Small synthetic CSV files for specific test scenarios
- `real_data/`: Larger datasets for performance/stress testing
