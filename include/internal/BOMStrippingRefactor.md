@page bom_stripping_refactor BOM Stripping Refactor

# BOM Stripping Refactor

**Status:** implemented in v5.5.0.

## Context

`CSVParserCore` used to own BOM detection through `ParserChunkOptions::scan_bom`.
Every core instance (the serial parser, each speculative worker, the repair
parser, and each format-guessing parser) kept its own "already scanned" flag and
stripped the BOM by advancing its read position inside the chunk.

That put a source-window concern inside the byte parser, and it caused three bugs:

- **`utf8_bom()` was wrong for speculative stream input.** The first stream
  window is the 500KB head plus a full read window, so it can be routed to
  `parse_speculative_window()`. A speculative worker stripped the BOM and
  recorded it on itself, but `CSVParseOrchestrator::utf8_bom()` read the serial
  parser's flag, which never saw the BOM.
- **The first row included the BOM.** `CSVParserCore::parse()` set the row start
  to 0 *before* stripping advanced `data_pos_`. So the first row's `raw_str()`
  began with `EF BB BF` and its `byte_offset()` was 0. Field values were correct,
  because field starts were computed from `data_pos_`.
- **A re-read first window kept the BOM.** When the mmap head window completed
  no row (issue #337), `MmapParser` rewound to offset 0 and parsed a full window.
  The serial core had already marked its scan as done, so the BOM bytes became
  part of the first column name.

## Design

BOM handling is a window-boundary concern owned by `CSVParseOrchestrator`
(`parser/orchestrator.hpp`):

1. On the first `parse_window()` call only, `get_bom_skip_or_throw()` inspects
   the leading bytes. A flag records that the scan happened; there is no
   inference from `base_offset == 0`.
2. UTF-16 and UTF-32 BOMs throw before any serial or speculative parser sees the
   window.
3. A UTF-8 BOM sets the orchestrator's `utf8_bom_` and is skipped. The parser
   receives `chunk.substr(skip)` and `base_offset + skip`.
4. The returned `CSVParseWindowResult` adds the skipped bytes back into
   `complete_prefix_length` and also reports them as `skipped_prefix_length`.

`CSVParserCore` and the speculative chunk types no longer carry any BOM state.
`ParserChunkOptions` is just `(initial_state, source_start)`.

Format guessing (`parser/guessing.cpp`) strips the BOM itself, once per
`guess_format()` call rather than once per candidate delimiter. It therefore
still rejects UTF-16/UTF-32 during `CSVReader` construction.

The first window is always the adapter's head buffer, which holds
min(source size, 500KB) bytes. So a first window too short to classify a BOM
happens only at end of input, and the scan does not need to defer.

## Offset Invariants

Source adapters speak in original source-byte offsets. Parser cores see a
BOM-free view.

- `RawCSVData::source_start` describes the original source position of the view
  the core parsed. For the first window of a BOM file this is `base_offset + 3`,
  so the first row's `byte_offset()` is 3 and `raw_str()` excludes the BOM.
- Field offsets stored in `RawCSVData` are relative to the backing view they
  reference, which is unchanged by the skip.
- `complete_prefix_length` includes skipped BOM bytes. `MmapParser`'s
  `mmap_pos -= (length - complete_prefix_length)` and `StreamParser`'s
  `leftover_` / `stream_pos_` handling therefore advance past the BOM even when
  no row completed, so a re-read never starts at the BOM.
- Code that needs to know whether a window completed a row must use
  `CSVParseWindowResult::completed_row()`, not position arithmetic. A skipped
  BOM advances the source position without completing a row. `MmapParser::next()`
  relies on this to decide whether the head buffer needs a follow-up read
  (issue #337).
- A BOM-only first window produces no row and still advances past the BOM.

## Non-goals

- Parsing UTF-16 or UTF-32 directly, or transcoding automatically. Those inputs
  are rejected with the "use a transcoder first" error.
- Changing delimiter handling, quoting behavior, or row materialization.
- Making `CSVReader::iterator` multi-pass or caching source chunks.

## Test Coverage

`tests/test_read_csv.cpp` (`[read_unicode_bom]`) covers each case through both
the mmap and stream constructors:

- UTF-8 BOM stripping with guessed and explicit formats, and `utf8_bom()`.
- Only one leading BOM is stripped. A second BOM stays in the first field, which
  guards against double stripping.
- UTF-16 LE/BE and UTF-32 LE/BE rejection, including UTF-32 LE (`FF FE 00 00`)
  not being reported as UTF-16.
- Empty input, a BOM-only file, and a BOM followed by one unterminated row.
- The first row's `raw_str()` and `byte_offset()` exclude the BOM.
- A 600K-row BOM file crossing chunk boundaries with serial parsing and with
  speculative parsing forced on.
- `guess_format()` on BOM and UTF-16 files.

`tests/test_edge_cases_large_rows.cpp` (`[issue_337]`) covers a UTF-8 BOM before
a header row longer than the mmap head buffer.
