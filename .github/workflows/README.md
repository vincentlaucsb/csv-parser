# GitHub Actions Memory Checking Setup

This directory contains GitHub Actions workflows for comprehensive memory and thread safety testing.

## Workflows

### 🧵 sanitizers.yml - Memory & Thread Sanitizers (PRIMARY)
**Runs:** On every push and pull request  
**Sanitizers Included:**
- **ThreadSanitizer (TSan)** - Detects data races and thread safety issues (CRITICAL for csv-parser)
- **AddressSanitizer (ASan)** - Detects memory errors: use-after-free, buffer overflows, memory leaks
- **UndefinedBehaviorSanitizer (UBSan)** - Catches undefined behavior: signed overflow, type mismatches

**Config:**
- Runs on Ubuntu with GCC
- Tests C++17 and C++20 standards
- Debug builds for better diagnostics
- Timeout: 20 minutes per configuration
- Artifacts: Upload logs on failure

**Key Features:**
- Matrix testing: 3 sanitizers × 2 C++ standards = 6 parallel jobs
- Fail-fast disabled to see all results
- Environment variables configured for halt-on-error behavior

### 💾 valgrind.yml - Valgrind Memory Profiler
**Runs:** On every push and pull request  
**Uses:** Valgrind with full leak checking

**Config:**
- Runs on Ubuntu with GCC
- Debug build with -O1 for balance
- Excludes multi-threaded tests (Valgrind + threads = slow)
- Timeout: 60 minutes (Valgrind is slower)

### 🔍 codeql.yml - Static Analysis (GitHub CodeQL)
**Runs:** On every push and pull request + weekly schedule  
**Analysis:**
- Deep static analysis for security and quality issues
- Integrates with GitHub Security tab

## Testing Recommendations

### Local Testing Before Push
```bash
# Test with ThreadSanitizer (most critical)
cmake -B build/tsan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build/tsan
cd build/tsan && ctest --output-on-failure && cd ../..

# Test with AddressSanitizer
cmake -B build/asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g"
cmake --build build/asan
cd build/asan && ctest --output-on-failure && cd ../..
```

### CI/CD Pipeline Order
1. **Primary:** ThreadSanitizer - Catches data races in threading model
2. **Primary:** AddressSanitizer - Catches memory corruption
3. **Secondary:** UBSanitizer - Catches subtle undefined behavior
4. **Nightly:** Valgrind - Comprehensive memory profiling
5. **Continuous:** CodeQL - Static analysis

## Known Considerations

### ThreadSanitizer (TSan)
- **Why CRITICAL:** csv-parser uses worker threads + ThreadSafeDeque + double_quote_fields lazy init
- May report false positives in STL (benign race detection)
- Slower execution due to extra instrumentation
- Catches real issues in PR #282 exception propagation and issue #278 move semantics

### AddressSanitizer (ASan)
- **Why Important:** Catches CSVFieldList memory issues like issue #278
- Cannot run simultaneously with TSan (different memory models)
- Better performance than TSan for memory safety

### Valgrind
- Slower than sanitizers but more mature tool
- Useful for final verification before releases
- Runs on push (not PR) to avoid GitHub Actions timeout

### CodeQL
- No runtime overhead
- Integrated into GitHub Security tab
- Good for catching security issues and code quality

## Interpreting Results

### Sanitizer Failures
Look for:
```
ERROR: ThreadSanitizer: data race detected
ERROR: AddressSanitizer: heap-buffer-overflow
ERROR: UndefinedBehaviorSanitizer: signed integer overflow
```

### Action Items
1. Check artifacts uploaded on test failure
2. Look at the specific sanitizer output in GitHub Actions logs
3. Focus on ThreadSanitizer results first (threading complexity)
4. Correlate with codebase changes to identify root cause

## History

- **PR #282:** ThreadSanitizer would catch exception propagation issues
- **Issue #278:** AddressSanitizer catches CSVFieldList move semantics bug
- **PR #237:** TSan catches double_quote_fields concurrency todo
- **v2.3.0:** ThreadSafeDeque prevents std::vector reallocation race

## Reference

- [ThreadSanitizer Documentation](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual)
- [AddressSanitizer Documentation](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [Valgrind User Manual](https://valgrind.org/docs/manual/)
- [CodeQL Query Help](https://codeql.github.io/codeql-query-help/)
