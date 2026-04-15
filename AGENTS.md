# CSV Parser - AI Agent Context

Architectural overview for AI assistants working with this codebase.

> **Maintenance rule:** Whenever this file is changed, update both `CLAUDE.md` and `ARCHITECTURE.md` in the same directory to reflect relevant changes. `CLAUDE.md` is a bullet-point summary and `ARCHITECTURE.md` is the top-level architecture index; both must stay in sync with this guidance.

## Critical: single_include/csv.hpp Is A Shim

`single_include/csv.hpp` is intentionally **non-functional** and exists only as a compatibility shim.

- Do **not** compile against `single_include/csv.hpp`
- For single-header validation, generate `build/.../single_include_generated/csv.hpp` via the `generate_single_header` target, then compile that generated file
- For unamalgamated usage, include headers from `include/`

This guard exists to prevent stale-in-repo amalgamated headers and to force use of the canonical generated distribution.

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
6. **Don't delete or simplify comments** unless they are trivially obvious (e.g. `// increment i`) or factually incorrect. Comments in this codebase frequently encode concurrency invariants, non-obvious design decisions, and hard-won bug context that cannot be recovered from the code alone.

See `tests/AGENTS.md` for test strategy, checklist, and conventions.
