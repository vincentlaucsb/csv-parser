@page bom_stripping_refactor BOM Stripping Refactor

# BOM Stripping Refactor

## Context

`CSVParserCore` currently owns BOM detection through `ParserChunkOptions::scan_bom`.
That works, but it means BOM handling is still part of the lower-level byte parser
instead of the source/window orchestration layer. `MmapParser` and `StreamParser`
now share `CSVParserDriverBase::utf8_bom()`, but the actual scan/strip behavior
still happens inside the parser core used by serial and speculative parsing.

The next architectural cleanup is to make BOM handling a window-boundary concern:
detect a Unicode BOM once, reject unsupported encodings early, strip a UTF-8 BOM
before parsing, and feed BOM-free byte views into both serial and speculative
parsers.

## Current Behavior

These results were checked on 2026-10-10 against `master` at `eee1732` with a
probe built from `include/`, on both constructor paths:

| Scenario | mmap | stream |
|---|---|---|
| UTF-8 BOM, serial parse | stripped, `utf8_bom()` true | stripped, `utf8_bom()` true |
| UTF-8 BOM, speculative parse (4 workers) | stripped, `utf8_bom()` true | stripped, **`utf8_bom()` false** |
| No-header BOM file, first row `raw_str()` | **includes `EF BB BF`** | **includes `EF BB BF`** |
| No-header BOM file, first row `byte_offset()` | 0 | 0 |
| BOM-only file | 0 rows, `utf8_bom()` true | 0 rows, `utf8_bom()` true |

Where the BOM bytes leak or get misreported:

- **Stream + speculative `utf8_bom()` bug.** The first stream window is the
  500KB head plus a full read window, so it is larger than `serial_chunk_size`
  and goes to `parse_speculative_window()`. A speculative worker core strips the
  BOM and records `utf8_bom_` on itself, but `CSVParseOrchestrator::utf8_bom()`
  reads `serial_parser_.utf8_bom()`, which never saw the BOM. mmap is not
  affected because its first window is the head buffer, which is never larger
  than one serial chunk.
- **First row includes the BOM.** `CSVParserCore::parse()` sets
  `current_row_start() = 0` *before* `strip_unicode_bom()` advances `data_pos_`.
  So the first row's raw span still covers the BOM. Field values are unaffected,
  because `field_start_` is computed from `data_pos_`.
- **Core-level scan state is per core instance.** `unicode_bom_scan_` lives on
  each `CSVParserCore`: the serial parser, every speculative worker parser, the
  repair parser, and each guessing parser. The orchestrator also re-derives "is
  this the first window" from `base_offset == 0`. Today that's correct only
  because a window that completes no rows makes `CSVReader` throw
  "row larger than chunk size" before any rewind to offset 0 can re-parse.

Format guessing (`parser/guessing.cpp`) builds its own `CSVParserCore` per
candidate delimiter. With the default `scan_bom = true` it strips the BOM, and
it throws on UTF-16/UTF-32 during `CSVReader` construction.

## Goals

- Keep `MmapParser` and `StreamParser` behavior identical.
- Detect UTF-8, UTF-16, and UTF-32 BOMs before CSV parsing begins.
- Strip UTF-8 BOM bytes before parser-core field/row offsets are produced.
- Reject UTF-16/UTF-32 with the existing "use a transcoder first" style error.
- Keep `CSVReader::utf8_bom()` reporting behavior.
- Preserve bounded-memory stream parsing and mmap chunk-remainder semantics.

## Non-goals

- Do not parse UTF-16 or UTF-32 directly.
- Do not add automatic transcoding.
- Do not change delimiter handling, quoting behavior, or row materialization.
- Do not make `CSVReader::iterator` multi-pass or cache source chunks.

## Proposed Shape

Move BOM scan state into the common parse orchestration path, likely
`CSVParseOrchestrator`, or a small helper owned by it.

For the first source window only:

1. Inspect the leading bytes with the shared BOM utility.
2. Throw on UTF-16/UTF-32 BOMs.
3. Record whether a UTF-8 BOM was present.
4. Pass `chunk.substr(skip)` to the serial or speculative parser.
5. Adjust returned `CSVParseWindowResult::complete_prefix_length` by the skipped
   byte count so source adapters still advance by source-byte offsets.

After that first scan, all parser-core invocations should receive
`ParserChunkOptions(..., false)` or an equivalent path that disables core-level
BOM scanning.

## Offset Invariants

The key invariant is that source adapters speak in original source-byte offsets,
while parser cores may see a BOM-free view.

- `base_offset` passed into parsing should still describe the original source.
- Field offsets stored in `RawCSVData` must remain correct relative to the
  backing chunk view they reference.
- `complete_prefix_length` returned to source adapters must include any stripped
  BOM bytes, otherwise mmap/stream remainder handling will re-read or retain the
  wrong prefix.
- If the first window is only a UTF-8 BOM, the parser should not manufacture a
  row and should still advance past the BOM.
- BOM rejection must happen before speculative workers are launched.

## Test Plan

Add focused tests for both constructor paths using Catch2 `SECTION`s:

- UTF-8 BOM is stripped for mmap input.
- UTF-8 BOM is stripped for stream input.
- `CSVReader::utf8_bom()` is true after reading UTF-8 BOM input.
- UTF-16 LE/BE BOMs throw for mmap input.
- UTF-16 LE/BE BOMs throw for stream input.
- UTF-32 LE/BE BOMs throw if supported by the BOM utility.
- Empty file, BOM-only file, and BOM followed by a single row behave consistently.
- A large UTF-8 BOM file crossing the 10MB chunk boundary preserves row data and
  chunk remainder behavior.
- The same large file with speculative parsing forced on
  (`speculative_parallel_threads(4)`, `speculative_parallel_min_bytes(0)`)
  reports `utf8_bom()` on both paths. This fails on stream input today.
- A no-header BOM file's first row has `raw_str() == "1,2,3"` and
  `byte_offset() == 3`. Both fail today.
- A double BOM (`EF BB BF EF BB BF`) strips exactly one BOM, and the second
  stays in the first field. This guards against double stripping by the
  orchestrator and the core.
- UTF-32 LE (`FF FE 00 00`) reports "UTF-32", not "UTF-16", on both paths.
- `csv::guess_format()` on a UTF-8 BOM file still guesses the delimiter and
  header, and still throws on UTF-16.

## Risks

- Off-by-skip errors can corrupt `stream_pos_`, `mmap_pos`, or `leftover_`.
- Speculative parsing may double-adjust offsets if both orchestrator and core
  scan BOMs.
- Empty or BOM-only files can expose EOF differences between mmap and stream
  paths.
- Tests that assert exact exception strings should use the shared exception
  message constants/helpers to avoid drift.

## Implementation Plan

The work splits into two PRs. PR 1 moves ownership and fixes the bugs described
above, while the old core plumbing stays in place but goes unused. PR 2 deletes
that plumbing. Splitting it this way means a regression in PR 1 can be bisected
without the noise from deleting code.

### PR 1: Orchestrator owns the BOM

**Step 1: Characterization tests** (`tests/test_read_csv.cpp`, `[read_unicode_bom]`)

Extend `TEST_CASE("Unicode BOM handling")` with one `SECTION` per constructor
path for every case in the Test Plan above. Use `FileGuard` temp files for mmap
and `std::istringstream` for stream. Build the large-file cases with ≥500K rows
and distinct per-column values, as `tests/AGENTS.md` requires. Check row
contiguity, not just row count.

Three of these tests fail on `master`: stream + speculative `utf8_bom()`, first
row `raw_str()`, and first row `byte_offset()`. Commit them in the same commit
as Step 2 so every commit stays green.

**Step 2: First-window BOM scan in `CSVParseOrchestrator`** (`parser/orchestrator.hpp`)

- Add private state `bool bom_scanned_ = false;` and `bool utf8_bom_ = false;`.
- At the top of `parse_window()`, before the serial/speculative dispatch:

  ```cpp
  size_t bom_skip = 0;
  if (!this->bom_scanned_) {
      bom_skip = get_bom_skip_or_throw(chunk, this->utf8_bom_);
      this->bom_scanned_ = true;
  }
  chunk = chunk.substr(bom_skip);
  base_offset += bom_skip;
  // ... dispatch exactly as today, using the BOM-free view ...
  result.complete_prefix_length += bom_skip;
  ```

- `utf8_bom()` returns `this->utf8_bom_` instead of `serial_parser_.utf8_bom()`.
  This fixes the stream + speculative bug.
- Use a flag rather than `base_offset == 0` for the "first window" test, so a
  future rewind to offset 0 can't scan twice.
- Throwing here happens before `parse_speculative_window()` dispatches any
  worker tasks, which satisfies the "reject before workers" invariant.
- The scan runs on whatever the first window contains. Both adapters guarantee
  that the first window is the head buffer, which holds min(source size, 500KB)
  bytes. So a window shorter than 4 bytes only happens when the source is
  exhausted. Write this assumption down in a comment and don't add deferral logic.

Offsets after this change:

- `RawCSVData::source_start` for the first block becomes `base_offset + 3`.
  `CSVRow::byte_offset()` of the first row becomes 3 and `raw_str()` excludes
  the BOM. Rows after the first are unchanged, because `source_start` and the
  view origin move together.
- `complete_prefix_length` stays in source-window bytes, so `MmapParser`'s
  `mmap_pos -= (length - cpl)` and `StreamParser`'s
  `leftover_ = chunk.substr(cpl)` / `stream_pos_ += cpl` need no changes.
- In a BOM-only window the parser sees an empty view: no row is produced,
  `cpl` is 3, and on exhaustion `end_feed()` has no partial row to flush.
  Before relying on this, confirm that `parse_prepared_chunk()` tolerates an
  empty `chunk`, which means calling `reserve_for_source_size(0)` and entering
  the `parse()` loop with `in.size() == 0`.

**Step 3: Stop core-level scanning on orchestrated calls**

- `parse_serial_window()` currently calls the
  `parse_chunk(chunk, owner, output, size_t source_start)` overload, which
  hard-codes `ParserChunkOptions(initial_state_, true, source_start)`. The core
  has no public getter for `initial_state_`, so add
  `ParserDFAState initial_state() const noexcept` next to `ending_state()`.
  Then call the options overload with
  `ParserChunkOptions(serial_parser_.initial_state(), false, base_offset)`.
  In PR 2 the `source_start` overload loses its `true` argument along with the
  rest of the plumbing.
- `parse_speculative_window()`: pass `false` for `scan_bom_for_first_chunk`
  instead of `base_offset == 0`.
- The double-BOM test is what proves this step works: if the core still scans,
  it strips the second BOM.

**Step 4: Format guessing** (`parser/guessing.cpp`)

In `guess_format(head, delims)`, strip once before the per-delimiter loop:

```cpp
bool ignored = false;
head = head.substr(get_bom_skip_or_throw(head, ignored));
```

This keeps UTF-16/UTF-32 rejection in the `CSVReader` constructor even after
the core loses its scan. It also scans once per guess instead of once per
candidate delimiter. Until PR 2 the guessing core still scans too, which is
harmless because the head no longer starts with a BOM.

**Validation for PR 1**

- Full `ctest` with the default configuration (threads on).
- `-DCSV_ENABLE_THREADS=OFF`, which compiles out the `#if CSV_ENABLE_THREADS`
  branches in the orchestrator.
- The generated single header (`generate_single_header` target), compiled
  directly. Don't compile `single_include/csv.hpp`.
- C++11 build. `csv::string_view::substr` and the new members must not need
  C++14 or later.

### PR 2: Remove core BOM plumbing

Delete the following, then `grep -rn "scan_bom\|unicode_bom\|strip_unicode_bom" include tests`
and confirm nothing remains:

| File | Remove |
|---|---|
| `parser/core.hpp` | `ParserChunkOptions::scan_bom` and its constructor parameter; `scan_bom_for_current_chunk_`, `unicode_bom_scan_`, `utf8_bom_`, `strip_unicode_bom()`, `utf8_bom()`; the reset in `finish_parse()`; the `scan_bom` branch in `parse()`; the `true` argument in the `parse_chunk` overloads |
| `parser/core.hpp` | Move the `get_bom_skip_or_throw` declaration to `parser/driver.hpp`, since the core no longer uses it. The definition stays in `driver.cpp`, and the function stays in `csv::internals` because tests call it there. |
| `parser/driver.hpp` | The `CSVParserCore<>::utf8_bom()` fallback in `CSVParserDriverBase::utf8_bom()`. Return `false` when there is no orchestrator. |
| `speculative/parallel_parser.hpp` | `SpeculativeParseChunk::scan_bom`, the `scan_bom_for_first_chunk` parameter, `result.scan_bom = ...` |
| `speculative/chunks.hpp` | `ParsedChunkRows::scan_bom` and its assignment in `split_parsed_chunk_rows()`; the `scan_bom` arguments at both `ParserChunkOptions` call sites |
| `tests/test_speculative_parser.cpp` | `first.scan_bom` / `second.scan_bom` assignments (two tests) |

**Hazard:** shrinking `ParserChunkOptions(state, bool scan_bom, size_t source_start)`
to `(state, size_t source_start)` lets a missed two-argument call like
`ParserChunkOptions(state, true)` compile silently as `source_start = 1`. Before
deleting the parameter, temporarily mark the old 3-argument constructor
`= delete`, or rename it, and let the compiler list every call site.

### Documentation (in PR 2)

- `include/internal/ARCHITECTURE.md`: remove "BOM handling" from the
  `CSVParserCore` bullet and add it to the orchestrator's responsibilities.
- `include/internal/JOURNEY_OF_A_CSV_FIELD.md`: remove "UTF-8 BOM skip" from the
  core's list and the `scan_bom` lines from the speculative chunk diagram. Add a
  "first window: BOM scan/strip" step before dispatch.
- This page: replace "Current Behavior" with a short "Resolved" note. Keep
  Offset Invariants as durable reference material.
- The PR description must call out the user-visible changes: the first row's
  `raw_str()` and `byte_offset()` move past the BOM, and `utf8_bom()` is now
  correct for speculative stream input. `README.md` needs no change.

### Out of Scope

- An mmap source whose first row is longer than the 500KB head buffer, with a
  larger `chunk_size()`, silently yields zero rows and no column names. This
  happens with or without a BOM, and stream input handles the same file
  correctly. Track it separately. Once it's fixed, the
  `complete_prefix_length += bom_skip` adjustment from Step 2 is what keeps the
  re-read window from parsing the BOM as data.
