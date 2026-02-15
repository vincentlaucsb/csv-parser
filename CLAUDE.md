# CSV Parser - AI Agent Context

Architectural overview for AI assistants working with this codebase.

## Critical: Two Independent Code Paths

The `CSVReader` class has **two completely different implementations**:

```cpp
// PATH 1: Memory-mapped I/O (MmapParser)
CSVReader reader("filename.csv");

// PATH 2: Stream-based (StreamParser)
std::ifstream infile("filename.csv", std::ios::binary);
CSVReader reader(infile, format);
```

**Impact:** Bugs can exist in one path but not the other (see issue #281). Any test validating parsing behavior must test BOTH paths using Catch2 `SECTION`.

## Threading: Worker + 10MB Chunks

- Worker thread reads in 10MB chunks (`ITERATION_CHUNK_SIZE`)
- Communicates via `ThreadSafeDeque<CSVRow>`
- Exceptions propagate via `std::exception_ptr`
- Critical: Fields spanning chunk boundaries must not corrupt

**Testing requirement:** Use ≥500K rows to cross 10MB boundary.

## Test Strategy: Use Distinct Column Values

❌ **BAD:** `array{i, i, i, i, i}` - All columns identical  
✅ **GOOD:** `array{i*5+0, i*5+1, i*5+2, i*5+3, i*5+4}` - Each column distinct

**Why:** Field corruption is only detectable if columns have different values.

## Key Files

| File | Contains |
|------|----------|
| `csv_reader.hpp` | Mmap vs stream constructors |
| `csv_reader.cpp` | Delimiter guessing, header detection |
| `basic_csv_parser.hpp` | Parser base class (IBasicCSVParser, MmapParser, StreamParser) |
| `basic_csv_parser.cpp` | Chunk transitions, worker thread |
| `raw_csv_data.hpp` | Internal parser data structures (RawCSVField, CSVFieldList, RawCSVData) |
| `thread_safe_deque.hpp` | Producer-consumer queue for parser→main thread communication |
| `csv_row.hpp` | Public API types (CSVField, CSVRow) |
| `test_round_trip.cpp` | Exemplar test patterns |

## Data Flow: Parser → Row API

```
Parser Thread                      Main Thread
     ↓                                  ↓
RawCSVData (shared_ptr) ─────────────→ CSVRow
     ↓                                  ↓
CSVFieldList → RawCSVField[]       CSVField (lazy unescaping)
     ↓
ThreadSafeDeque<CSVRow>
(producer-consumer queue)
```

**Thread Safety:** Parser populates `RawCSVData`, pushes `CSVRow` to `ThreadSafeDeque`, main thread pops and reads. The `CSVFieldList` uses chunked allocation (~170 fields/chunk) for cache locality. See `raw_csv_data.hpp` and `thread_safe_deque.hpp` for implementation details.

## Common Pitfalls

1. **Don't assume one code path:** Mmap and stream paths are different. Always test both.
2. **Don't write tiny tests:** Need ≥500K rows to cross 10MB chunk boundary.
3. **Don't use uniform values:** Each column needs distinct values to detect corruption.
4. **Don't ignore async:** Worker thread means exceptions must use `exception_ptr`.
5. **Don't change one constructor:** Likely affects both mmap and stream paths.

## Test Checklist

- [ ] Tests both mmap and stream paths (use `SECTION`)
- [ ] Distinct values per column
- [ ] ≥500K rows to cross chunk boundary
- [ ] Documents bug it would catch
- [ ] Lambda + SECTION pattern for code reuse
- [ ] Test data in `tests/data/fake_data` (real data in `tests/data/real_data`)
- [ ] Use `FileGuard` for temporary files (ensures cleanup even if test fails)

**Note:** `tests/data` is a git submodule. Remember to commit changes separately.

## Recent Bug Fixes

| Issue | Bug | Fixed |
|-------|-----|-------|
| #278 | CSVFieldList move constructor dangling pointer | Feb 2026 |
| #280 | Field corruption at chunk boundaries | PR #282 |
| #281 | Stream-specific exception handling | PR #282 |
| #283 | Header detection with variable-width rows | Jan 2026 |
| #285 | Delimiter guessing overwrites `no_header()` | Feb 2026 |

See inline comments in source files for implementation details.
