## Test Architecture

### Framework
- **Catch2 v3.6.0**: Modern C++ testing framework with SECTION support for testing multiple code paths

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
Tests use RAII cleanup via [FileGuard](shared/file_guard.hpp):
```cpp
TEST_CASE("My test") {
    FileGuard cleanup("test.csv");  // Destructor deletes file on scope exit
    // ... test code ...
}  // File cleaned up even if assertions fail
```

### Test Files

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
