# CSV Parser - Test Agent Context

> **Maintenance rule:** Whenever this file is changed, `CLAUDE.md` in the same directory must be updated to reflect the changes. `CLAUDE.md` is a bullet-point summary of this file and must stay in sync.

## Test Checklist

- [ ] Tests both mmap and stream paths (use `SECTION`)
- [ ] Distinct values per column
- [ ] ≥500K rows to cross chunk boundary
- [ ] Documents bug it would catch
- [ ] Lambda + SECTION pattern for code reuse
- [ ] Test data in `tests/data/fake_data` (real data in `tests/data/real_data`)
- [ ] Use `FileGuard` for temporary files (ensures cleanup even if test fails)

**Note:** `tests/data` is a git submodule. Remember to commit changes separately.

## Test Strategy: Use Distinct Column Values

❌ **BAD:** `array{i, i, i, i, i}` - All columns identical  
✅ **GOOD:** `array{i*5+0, i*5+1, i*5+2, i*5+3, i*5+4}` - Each column distinct

**Why:** Field corruption is only detectable if columns have different values.

## Test Architecture

### Framework
- **Catch2 v3.6.0**: Modern C++ testing framework with SECTION support for testing multiple code paths

### Shared Test Utilities (`tests/shared/`)

**Always check `tests/shared/` before implementing any test helper from scratch.**

| File | Purpose |
|------|---------|
| `shared/file_guard.hpp` | RAII temp-file cleanup — **use this for every temp file** |
| `shared/float_test_cases.hpp` | Shared floating-point edge-case data |
| `shared/timeout_helper.hpp` | Timeout wrapper for race/stress tests to prevent hangs |

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

### Testing Conventions

#### Tests Should Expose Bugs, Not Assert Them

When writing a test for a known bug, assert correct behavior (even if it currently fails), not buggy behavior.

Wrong pattern (do not use):

```cpp
TEST_CASE("Issue #123", "[bug]") {
  REQUIRE(result == "wrong_value");
}
```

Right pattern:

```cpp
TEST_CASE("Issue #123", "[bug][!shouldfail]") {
  REQUIRE(result == "correct_value");
}
```

Why:
- Bug is visible immediately as a failing test
- Test auto-passes once bug is fixed
- No TODO/update cycle required

#### Catch2 Tags for Known Failing Tests

- Expected failing bug test: `[bug][!shouldfail]`
- Or skip by default: `[.][bug]`

#### Placement Rule: Edge Cases and Regressions at End of File

In each test file:
- Mainline/general feature tests first
- Edge-case and regression tests last

This keeps the top of files focused on broad feature coverage and groups known edge cases in one place.

#### Pattern for Known-Bug Regression Tests

```cpp
TEST_CASE("Feature XYZ - Issue #N", "[issue_N][!shouldfail]") {
  // Expected: X
  // Actual (buggy): Y
  auto result = buggy_function();
  REQUIRE(result == correct_value);
}
```

#### Temporary File Cleanup Must Use RAII

Never use manual `std::remove()` cleanup in tests.

Always use `FileGuard` from `shared/file_guard.hpp` so files are cleaned up even if assertions fail.

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

#### Timeout Guard for Race/Stress Tests

Use [test_with_timeout](shared/timeout_helper.hpp) for tests that may hang under deadlock regressions.

```cpp
#include "shared/timeout_helper.hpp"

SECTION("Race-sensitive scenario") {
  test_with_timeout([]() {
    // loop / iterator logic that should complete quickly
  });
}
```

This gives explicit failures instead of CI hangs when synchronization regresses.

#### Multithreaded Testing: Thread-Safe Assertions

**⚠️ CRITICAL:** Catch2 is **not thread-safe** for assertions. Do NOT call `REQUIRE()`, `CHECK()`, or other assertion macros from worker threads.

**The Problem:**
- Catch2 asserts access the main thread's stack
- When worker threads also call asserts, both threads write to the same memory
- ThreadSanitizer detects this as a data race
- Test hangs or times out with spurious race conditions

**The Solution: ThreadSafeErrorCollector Pattern**

Collect failures in the worker thread, assert in the main thread.

```cpp
#include "shared/timeout_helper.hpp"

TEST_CASE("Multithreaded parsing", "[threading]") {
    auto errors = std::make_shared<ThreadSafeErrorCollector>();
    
    test_with_timeout([errors]() {
        for (int i = 0; i < 100; i++) {
            std::stringstream ss("A,B\n1,2\n");
            CSVReader reader(ss);
            
            CSVRow row;
            if (!reader.read_row(row)) {
                errors->add_error("Failed to read row");
            }
            
            auto val = row["A"].get<int>();
            if (val != 1) {
                errors->add_error("A != 1, got " + std::to_string(val));
            }
        }
    });
    
    errors->check_and_fail_if_errors();  // Main thread asserts
}
```

**Key Rules:**
1. Create error collector in main thread (before test_with_timeout)
2. Capture `[errors]` in the lambda
3. **Inside the lambda: never call REQUIRE/CHECK** — use `errors->add_error(msg)` instead
4. **After test_with_timeout returns: call `errors->check_and_fail_if_errors()`** — main thread does the assertion

**Why this works:**
- Worker thread only does string operations (thread-safe)
- Main thread is the only caller of Catch2 assertion APIs
- ThreadSanitizer sees no data races
- Failures are still captured and reported accurately

**Example from codebase:** See [test_threadsafe_deque_race.cpp](test_threadsafe_deque_race.cpp) for full patterns.

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
4. **Timeout Guards**: Use `test_with_timeout()` for race/deadlock-sensitive tests
5. **Distinct Values**: Detect cross-field corruption
6. **Chunk Boundary Testing**: Cross 10MB ITERATION_CHUNK_SIZE

### Data Files
Test data in `tests/data/` is a git submodule:
- `fake_data/`: Small synthetic CSV files for specific test scenarios
- `real_data/`: Larger datasets for performance/stress testing
