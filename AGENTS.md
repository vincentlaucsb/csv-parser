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

- Worker thread reads in 10MB chunks (`CSV_CHUNK_SIZE_DEFAULT`)
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
7. **Compatibility macros defined in `common.hpp` MUST be referenced only after including `common.hpp`.** Any macro (such as `CSV_HAS_CXX20`) that is defined in `common.hpp` must not be used or checked before `#include "common.hpp"` appears in the file. This ensures feature detection and conditional compilation work as intended across all supported compilers and build modes.
8. **`CSVReader` is non-copyable and move-enabled.** Prefer explicit ownership transfer (`std::move`) or `std::unique_ptr<CSVReader>` when sharing/handing off parser ownership across APIs.
9. **Prefer trailing underscore for private members** (for example `source_`, `leftover_`). When you touch code with mixed private-member naming styles, normalize the edited region toward trailing underscores instead of introducing more leading-underscore or unsuffixed names.
10. **Prefer user-friendly API constraints.** Do not narrow template constraints unless required for correctness, safety, or a measured performance win. If an implementation already handles common standard-library containers/ranges correctly, keep those inputs accepted instead of over-constraining APIs for aesthetic purity.
11. **Respect existing compile-time compatibility macros.** Keep `IF_CONSTEXPR`, `CONSTEXPR_VALUE`, and similar macros unless there is a correctness bug.
12. **Do not replace compile-time constructs with runtime control flow to silence warnings.** Prefer smallest scoped warning suppression at the exact site (for example, local `#pragma warning(push/pop)` on MSVC) over semantic rewrites.
13. **Opportunistic rewrites/refactors are allowed when they are safe and justified.** Keep them separated from build-fix urgency where possible, and avoid bundling unrelated churn with compiler triage unless explicitly requested.
14. **When proposing changes that affect compile-time behavior, explain the tradeoff clearly.** Call out any impact to codegen, performance, portability, and readability before applying the change.
15. **If a build fix appears to require more than ~3 files or ~60 changed lines, pause and confirm scope first.** Provide a short justification before expanding further.

See `tests/AGENTS.md` for test strategy, checklist, and conventions.

### Rules for Comments
1. **Always update or remove incorrect comments.**
2. **Don't reference internal functions in public API comments.** Public API docs should describe user-visible behavior and contracts; internal helper/function details belong in internal docs.
3. **Avoid meaningless @param and @return descriptions.** Do not add comments that could trivially be inferred by the function's name or other existing comments. When editing a function, remove any @param/@return descriptions that merely restate the function name or signature.
4. **Don't delete or simplify comments** unless allowed by other rules in this section. Comments in this codebase frequently encode concurrency invariants, non-obvious design decisions, and hard-won bug context that cannot be recovered from the code alone.