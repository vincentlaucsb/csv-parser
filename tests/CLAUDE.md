# CSV Parser Tests - Claude Summary

> **`AGENTS.md` is the source of truth.** This file is a bullet-point summary only. Always load and follow `tests/AGENTS.md` — it takes precedence over anything here.

## Test Checklist
- [ ] Both mmap and stream paths tested (Catch2 `SECTION`)
- [ ] ≥500K rows to cross 10MB chunk boundary
- [ ] Distinct values per column (not `i, i, i, i, i`)
- [ ] `FileGuard` used for all temp files — never raw `std::remove()`
- [ ] New `test_*.cpp` files added to `target_sources()` in `tests/CMakeLists.txt`
- [ ] Test data in `tests/data/fake_data`; `tests/data` is a git submodule

## Key Conventions
- Lambda + `SECTION` pattern: write validation logic once, run on both paths
- Known-bug tests: assert correct behavior with `[bug][!shouldfail]`, not buggy behavior
- Edge-case and regression tests go at the **end** of each file
- Use `test_with_timeout()` from `shared/timeout_helper.hpp` for race/hang-sensitive tests
- **Multithreaded testing:** Never call `REQUIRE`/`CHECK` from worker threads — use `ThreadSafeErrorCollector` to collect errors, then assert in main thread (Catch2 is not thread-safe)

## Shared Utilities (`tests/shared/`)
- `file_guard.hpp` — RAII temp file cleanup (`FileGuard`, not `TempFile` or `ScopedFile`)
- `float_test_cases.hpp` — shared floating-point edge-case data
- `non_seekable_stream.hpp` — non-seekable `std::istream` test double (`NonSeekableStream`)
- `timeout_helper.hpp` — `test_with_timeout()` for deadlock-sensitive tests

## Distinct Column Values
- Bad: `array{i, i, i, i, i}` — corruption undetectable
- Good: `array{i*5+0, i*5+1, i*5+2, i*5+3, i*5+4}` — each column unique
