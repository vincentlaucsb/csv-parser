# Testing Conventions for AI Agents

## Rule: Tests Should Expose Bugs, Not Assert Them

When writing a test to expose a known bug:

❌ **WRONG - Don't assert buggy behavior:**
```cpp
TEST_CASE("Issue #123 - Parser fails", "[bug]") {
    // CURRENT BUGGY BEHAVIOR:
    REQUIRE(result == "wrong_value");  // Don't do this!
    
    // TODO: When fixed, change to:
    // REQUIRE(result == "correct_value");
}
```

**Problem:** Test passes with bug present, creating confusion. You must remember to update it later.

✅ **RIGHT - Assert correct behavior:**
```cpp
TEST_CASE("Issue #123 - Parser fails", "[bug][!shouldfail]") {
    // This test documents bug #123 and will FAIL until fixed
    REQUIRE(result == "correct_value");  // What it SHOULD be
}
```

**Benefits:**
- Test fails immediately, exposing the bug
- When bug is fixed, test automatically passes
- No TODO comments needed
- Clear signal that something is wrong

## Catch2 Tags for Failing Tests

Use these tags to mark tests that are expected to fail:

```cpp
TEST_CASE("Description", "[bug][!shouldfail]") { ... }
```

Or exclude from default runs:
```cpp
TEST_CASE("Description", "[.][bug]") { ... }  // Skip by default
```

## Test Pattern for Known Bugs

```cpp
TEST_CASE("Feature XYZ - Issue #N", "[issue_N][!shouldfail]") {
    // Describe the bug briefly
    // 
    // Expected: X
    // Actual (buggy): Y
    //
    // Common scenarios: ...
    
    auto result = buggy_function();
    
    // Assert CORRECT behavior (test will fail until bug is fixed)
    REQUIRE(result == correct_value);
}
```

## Why This Matters

1. **Immediate feedback:** Bug is visible as failing test
2. **Self-documenting:** When test passes, bug is fixed
3. **Less maintenance:** No need to update test after fix
4. **Clear intent:** Anyone reading test sees what's wrong
5. **CI integration:** Can track failing tests as known issues

## Example: Issue #283

Instead of:
```cpp
// Bad: Asserts buggy behavior
REQUIRE(col_names[0] == "1");  // BUG: Should be "a"
```

Write:
```cpp
// Good: Asserts correct behavior, test fails exposing bug
REQUIRE(col_names[0] == "a");  // Will fail until #283 is fixed
```

Mark test with `[!shouldfail]` tag so it's clear this is expected to fail.

## Temporary File Cleanup with RAII

When tests create temporary files, use RAII to ensure cleanup even if tests fail:

❌ **WRONG - Manual cleanup:**
```cpp
TEST_CASE("Test", "[test]") {
    std::ofstream out("temp.csv");
    // ... write data ...
    
    CSVReader reader("temp.csv");
    REQUIRE(reader.n_rows() == 100);  // If this fails, remove() never runs!
    
    std::remove("temp.csv");  // Won't execute if REQUIRE fails
}
```

✅ **RIGHT - RAII cleanup:**
```cpp
TEST_CASE("Test", "[test]") {
    auto filename = "temp.csv";
    FileGuard cleanup(filename);  // Always cleans up, even on failure
    
    std::ofstream out(filename);
    // ... write data ...
    
    CSVReader reader(filename);
    REQUIRE(reader.n_rows() == 100);  // File cleaned up regardless
}
```

**FileGuard implementation:**
```cpp
struct FileGuard {
    std::string filename;
    explicit FileGuard(std::string fname) : filename(std::move(fname)) {}
    ~FileGuard() { std::remove(filename.c_str()); }
    FileGuard(const FileGuard&) = delete;
    FileGuard& operator=(const FileGuard&) = delete;
};
```

This pattern is used in `test_round_trip.cpp` and `test_error_handling.cpp`.

