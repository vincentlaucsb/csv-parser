# CSV Parser - Claude Summary

> **`AGENTS.md` is the source of truth.** This file is a bullet-point summary only. Always load and follow `AGENTS.md` — it takes precedence over anything here.

## Maintenance Rule
- When `AGENTS.md` changes, update both `CLAUDE.md` and root `ARCHITECTURE.md` to keep guidance and architecture index references aligned.

## single_include/csv.hpp
- Non-functional shim — do **not** compile against it
- For single-header use: generate `build/.../single_include_generated/csv.hpp` via `generate_single_header` target
- For unamalgamated use: include from `include/`

## Two Independent Code Paths
- `CSVReader("file.csv")` → MmapParser
- `CSVReader(istream, format)` → StreamParser
- Bugs can exist in one and not the other — always test both with Catch2 `SECTION`

## Threading
- Worker thread reads 10MB chunks (`ITERATION_CHUNK_SIZE`)
- Communication via `ThreadSafeDeque<CSVRow>`
- Exceptions propagate via `std::exception_ptr`
- Tests must use ≥500K rows to cross chunk boundary

## Key Files
- `csv_reader.hpp` — mmap vs stream constructors
- `basic_csv_parser.hpp` — MmapParser, StreamParser implementations
- `basic_csv_parser.cpp` — chunk transitions, worker thread
- `raw_csv_data.hpp` — RawCSVField, CSVFieldList, RawCSVData
- `thread_safe_deque.hpp` — producer-consumer queue
- `csv_row.hpp` — CSVField, CSVRow public API

## Common Pitfalls
- Always test both mmap and stream paths
- ≥500K rows needed to cross 10MB boundary
- Use distinct column values to detect field corruption
- Exceptions from worker thread need `exception_ptr`
- Changes to one constructor likely affect both paths
- **Do not delete or simplify comments** unless trivially obvious or factually wrong — comments encode concurrency invariants and bug history
- **Compatibility macros defined in `common.hpp` MUST be referenced only after including `common.hpp`.** Any macro (such as `CSV_HAS_CXX20`) that is defined in `common.hpp` must not be used or checked before `#include "common.hpp"` appears in the file. This ensures feature detection and conditional compilation work as intended across all supported compilers and build modes.
- **Do not reference internal functions in public API comments** — public API docs should remain user-facing; internal details belong in internal docs
- **`CSVReader` is non-copyable and non-movable** — the preferred sharing/transfer idiom is `std::unique_ptr<CSVReader>`
- **Prefer trailing underscore for private members** — when touching mixed-style code, normalize the edited region toward names like `source_` and `leftover_`

## Tests
See `tests/AGENTS.md` for full test strategy, checklist, and conventions.
