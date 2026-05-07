# Temporary Test TODO

Temporary working notes from the Codecov API pass on branch `cinco_de_mayo`.
Delete this before release once the useful tests are either added or intentionally skipped.

## Codecov Snapshot

- Branch endpoint:
  `https://api.codecov.io/api/v2/github/vincentlaucsb/repos/csv-parser/branches/cinco_de_mayo/`
- Report endpoint pattern:
  `https://api.codecov.io/api/v2/github/vincentlaucsb/repos/csv-parser/report/?branch=cinco_de_mayo&path=<path>`
- Current branch coverage from Codecov: `88.53%`
- Totals: `2747` lines, `2432` hits, `105` misses, `210` partials
- Line coverage encoding:
  - `0` = hit
  - `1` = miss
  - `2` = partial

## Added Locally This Pass

In `tests/test_speculative_parser.cpp`:

- `Speculative scanner probability model can choose a quoted start`
- `Speculative scanner falls back to record-size heuristic when odds tie`
- `Speculative scanner record-size heuristic can choose a quoted start`

These target uncovered branches in `include/internal/speculative/scanner.hpp`:

- quoted-start probability path around lines `400-402`
- record-size heuristic path around lines `410-413`
- `observe_speculation()` record-size diagnostic path around line `55`
- quoted-start record-size heuristic outcome around line `412`

In `tests/test_read_csv_file.cpp`:

- `CSVReader::read_chunk with zero max rows does not consume data`
- `CSVReader move operations preserve unread rows`
- `CSVReader no_quote treats quote bytes as ordinary data`

These target useful public behavior in `include/internal/csv_reader.cpp` and
`include/internal/parser/driver.cpp`:

- `read_chunk(out, 0)` returns `false`, clears the output buffer consistently
  with other `read_chunk()` calls, and leaves rows readable.
- Move construction and move assignment preserve unread rows for both filename
  and stream-backed readers.
- `CSVFormat::quote(false)` reaches the no-quote parse-flag path and treats
  quote bytes as ordinary field data for both parser implementations.

In `tests/test_round_trip.cpp`:

- Added a local memoized generated-file helper for the expensive quoted
  round-trip fixture.
- Expanded the quoted edge-case round trip into the useful 2x2 matrix:
  mmap/stream source x serial/speculative parser mode.
- Kept the helper local for now; move it into `tests/shared` only if another
  expensive SECTION matrix needs the same pattern.
- This matrix exposed a real speculative-path bug: worker chunk parsers were
  not receiving the shared `ColNamesPtr`, so name lookup on speculative rows
  could dereference a null column-name pointer. Fixed by plumbing `ColNamesPtr`
  through `ParallelCSVParser` and `ChunkParserCore`.

## High-Value Remaining Targets

### `include/internal/speculative/scanner.hpp`

Coverage: `84.57%`, misses: `12`, partials: `17`

Worth testing:

- Ambiguous prefix where probability odds are exactly tied and the heuristic chooses quoted, not unquoted. Added locally.
- Empty-prefix speculation, if the behavior is considered a stable internal contract.
- Probability-model edge cases:
  - linear-root branch when `a` is effectively zero but `b` is not
  - negative discriminant branch returning neutral odds

Do not overdo this. The validator/fallback protects correctness; scanner tests should be small branch probes, not giant parser workflows.

### `include/internal/csv_parallel_parser.hpp`

Coverage: `83.21%`, misses: `14`, partials: `9`

Worth testing:

- Single-worker path through `ParallelCSVParser::parse_chunks()` using multiple chunks.
- Worker exception capture path around lines `283-289`.
- Fallback path in `parse_chunks_parallel()` when `workers_` is empty, if reachable without making internals weird.

Likely not worth testing:

- Thread scheduling micro-branches unless there is a concrete bug risk.

### `include/internal/parser/driver.cpp`

Coverage: `88.33%`, misses: `4`, partials: `3`

Worth testing:

- `CSVFormat::no_quote` path around lines `116-120`. Added locally through both `CSVReader` public paths.
- Filename `guess_format()`/header inference path if not already covered by public tests.

### `include/internal/parser/driver.hpp`

Coverage: `75.86%`, misses: `3`, partials: `4`

Worth testing:

- Base fallback accessors when no orchestrator exists:
  - `speculative_diagnostics()` returns empty diagnostics
  - `parse_worker_count()` returns `1`
  - `utf8_bom()` falls back to parser-core BOM state

Prefer reaching these through `MmapParser` or `StreamParser`; avoid creating test-only subclasses unless the public paths are painful.

### `include/internal/csv_reader.cpp`

Coverage: `87.09%`, misses: `6`, partials: `6`

Worth testing:

- `read_chunk(out, 0)` returns `false` and leaves the reader usable. Added locally.
- `CSVReader` move construction / move assignment preserve readable state. Added locally for both filename and stream readers.
- `CSVReader` move construction / move assignment preserve scheduler exception state. Deferred for now: a public-path test would need a carefully timed worker exception and is likely to be brittle compared with the existing worker-exception coverage.
- Variable-column `THROW` path for row-too-long and row-too-short, if not already covered in both mmap and stream paths.

### `include/internal/csv_read_scheduler.hpp`

Coverage: `84.37%`, misses: `6`, partials: `4`

Worth testing:

- `ThreadedCSVReadScheduler` exception adoption/rethrow path via a parser error on the worker.
- `CSVReadScheduler` routes adopted exceptions to the active concrete scheduler.

Keep these as behavior tests through `CSVReader` where possible. Direct scheduler tests are acceptable only if public coverage becomes too contorted.

### `include/internal/csv_exceptions.hpp`

Coverage: `78.0%`, misses: `10`, partials: `1`

Worth testing only where it corresponds to real behavior:

- Unsupported UTF-16/UTF-32 encoding messages are already meaningful.
- Stream read failure and mmap failure are harder to trigger portably; skip unless a clean test exists.
- Row too short / row too long messages are useful public behavior.

## Deferred / Intentionally Skipped This Pass

- Speculative scanner empty-prefix behavior: stable enough internally, but not
  user-visible and less valuable than the ambiguity/probability branches now
  covered.
- Speculative scanner linear-root and negative-discriminant math branches:
  possible only through very synthetic prefixes; skipped to avoid coverage
  theater unless a future parser bug points there.
- `ParallelCSVParser` worker exception capture and empty-worker fallback:
  the former requires manufacturing an internal parse failure inside a worker,
  while the latter is a defensive fallback that is not naturally reachable after
  construction. Existing public worker exception tests remain more meaningful.
- Base parser-driver fallback accessors with no orchestrator: already covered
  indirectly for serial stream behavior where practical; direct subclass tests
  would mostly assert default plumbing.
- Scheduler exception adoption through move: useful but timing-sensitive through
  public API; defer until there is a clean deterministic fixture.

## Coverage Triage Rules

- Prefer tests that exercise both mmap and stream paths when they validate parser behavior.
- Do not add tests solely to hit platform-specific or impossible-to-trigger defensive branches.
- Do not make production APIs worse for coverage.
- Favor short internal tests for speculative scanner math; favor public API tests for `CSVReader` behavior.
- If a test needs huge data, keep it just large enough to cross the relevant chunk boundary.
