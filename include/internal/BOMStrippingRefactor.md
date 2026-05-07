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

## Risks

- Off-by-skip errors can corrupt `stream_pos_`, `mmap_pos`, or `leftover_`.
- Speculative parsing may double-adjust offsets if both orchestrator and core
  scan BOMs.
- Empty or BOM-only files can expose EOF differences between mmap and stream
  paths.
- Tests that assert exact exception strings should use the shared exception
  message constants/helpers to avoid drift.

## Suggested Implementation Order

1. Add tests that document the current expected behavior.
2. Add orchestrator-level BOM scan state and result adjustment.
3. Disable parser-core BOM scanning from orchestrated parse calls.
4. Remove obsolete `scan_bom` plumbing only after tests prove it is no longer
   needed by serial or speculative paths.
5. Update `include/internal/ARCHITECTURE.md` if ownership of BOM handling moves
   from `CSVParserCore` to the orchestrator.
