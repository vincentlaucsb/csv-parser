# Journey of a CSV Field

This document follows one field from source bytes to user-facing `CSVField`.
It is intentionally a narrative debugging guide, not a component inventory.
Use it when a parsed value is wrong and you need to know where the bytes could
have changed shape.

## The Short Version

A CSV field is usually not copied while parsing.

1. A source adapter (`MmapParser` or `StreamParser`) provides a byte window.
2. `CSVParseOrchestrator` chooses serial parsing or speculative parallel parsing.
3. `CSVParserCore` walks bytes and records field boundaries in `RawCSVFieldList`.
4. `CSVRow` stores a shared pointer to `RawCSVData`.
5. `CSVField` slices the original bytes and materializes trim/unescape/conversion
   only when the user asks for it.

Speculative parsing changes how rows become safe to release. It does not change
the public `CSVRow` / `CSVField` ownership model.

The source-adapter layer lives in `include/internal/parser/` and
`csv::internals::parser`. The speculative-only helpers live in
`include/internal/speculative/` and `csv::internals::speculative`.

## Source Bytes Enter the Parser

There are two source paths:

- `MmapParser`
  - Used by the filename constructor on native builds.
  - Maps source windows and tracks file offsets.
  - Keeps mmap ownership alive through `RawCSVData` so `CSVRow` field slices do
    not dangle.

- `StreamParser`
  - Used by `std::istream` constructors and by the filename constructor on
    Emscripten.
  - Reads bytes into owned `std::string` windows.
  - Carries incomplete trailing rows in `leftover_` and prepends them to the
    next window.

Both paths feed byte windows into the same orchestrator/parser core. Bugs may
still exist in only one path because source ownership, window construction, and
remainder handling are different.

## Runtime Scheduling

`CSVReader` does not parse bytes directly. It asks `CSVReadScheduler` to run a
read cycle.

- With runtime threading enabled, the scheduler launches the read work on a
  worker thread and transfers exceptions back to the consumer thread.
- With `CSVFormat::threading(false)`, the scheduler runs the same read work
  synchronously on the caller thread.
- With `CSV_ENABLE_THREADS=0`, the scheduler is always synchronous.

Runtime threading opt-out also disables speculative parallel parsing for that
reader. The row and field ownership model is otherwise unchanged.

## Serial Path

Serial parsing is the simplest path:

```text
CSVReader
  -> CSVReadScheduler
  -> MmapParser / StreamParser
  -> CSVParseOrchestrator::parse_serial_window()
  -> CSVParserCore::parse_chunk()
  -> RowCollection
  -> CSVReader::read_row() / read_chunk() / iterator
  -> CSVRow
  -> CSVField
```

`CSVParserCore` owns the DFA state for this path. It handles:

- delimiter detection
- quote state
- escaped quote pairs
- CR, LF, and CRLF row endings
- UTF-8 BOM skip
- field boundary recording
- row emission

For each field, the parser records metadata instead of eagerly building a
`std::string`:

```text
RawCSVField {
  start,
  length,
  has_double_quote
}
```

The `start` and `length` point into `RawCSVData::data`, which views the backing
source window.

## Speculative Parallel Path

Speculative parsing is used only when threading is compiled in, runtime
threading is enabled, speculative parsing is requested, and the source is large
enough.

The high-level flow is:

```text
MmapParser / StreamParser window
  -> make_speculative_parse_chunks()
  -> SpeculativeScanner
  -> ParallelCSVParser worker parsers
  -> SpeculativeParseValidator
  -> RowCollection
  -> CSVReader consumer APIs
```

The key idea is that worker chunks need a guessed initial DFA state. The
speculator asks one question:

> Does this chunk probably start inside a quoted field?

The scanner uses lightweight quote/newline evidence and ambiguity heuristics to
choose an initial `ParserDFAState`. Worker parsers then parse chunks
independently.

The validator is the safety gate. It releases rows only after checking that the
previous chunk's ending state matches the next chunk's expected starting state.
If speculation was wrong, the validator repairs by reparsing the affected bytes
with the correct state.

After validation, rows are ordinary `CSVRow` objects. Consumer-side field access
does not know whether the row came from serial parsing or speculative parsing.

Low-level flow:

```text
source window bytes
  |
  v
make_speculative_parse_chunks()
  |
  +--> chunk 0 bytes + owner + offset + sequence_number
  |       |
  |       +--> starts_at_record_boundary = true
  |       +--> scan_bom = true
  |       +--> assumed_start_state = outside quotes
  |
  +--> chunk N bytes + owner + offset + sequence_number
          |
          v
      SpeculativeScanner::speculate()
          |
          +--> quote/newline prefix scan
          +--> q-o / o-q evidence
          +--> ambiguity counters
          +--> optional probability / size heuristic
          |
          v
      SpeculativeParseChunk
          |
          +--> bytes
          +--> owner
          +--> offset
          +--> sequence_number
          +--> assumed_start_state
          +--> starts_at_record_boundary
          +--> scan_bom

ParallelCSVParser::parse_chunks()
  |
  +--> worker parser 0
  |       |
  |       v
  |   CSVParserCore::parse_chunk(bytes, assumed_start_state)
  |       |
  |       v
  |   split_parsed_chunk_rows()
  |       |
  |       +--> prefix_fragment
  |       +--> complete_rows
  |       +--> suffix_fragment
  |       +--> ending_state
  |
  +--> worker parser 1 ... worker parser N
          |
          v
      ParsedChunkRows

SpeculativeParseValidator
  |
  +--> expected_start_state matches parsed initial state?
  |       |
  |       +--> yes:
  |       |      - join pending suffix with prefix fragment if needed
  |       |      - release complete_rows in sequence order
  |       |      - save suffix_fragment for next chunk
  |       |
  |       +--> no:
  |              - concatenate affected bytes/fragments
  |              - reparse with correct ParserDFAState
  |              - release repaired rows
  |              - increment repair diagnostics
  |
  v
RowCollection::append_rows()
  |
  v
CSVReader consumer APIs
```

Important ownership detail: each speculative chunk carries an `owner` shared
pointer for the bytes it parsed. When the validator repairs by concatenating
fragments, the repaired bytes receive their own owner. Either way, released
`CSVRow` objects keep their backing bytes alive through `RawCSVData`.

## Split Rows and Fragments

Chunk boundaries can split a CSV record. This is especially common when quoted
fields contain embedded newlines.

Speculative parsing represents edge pieces explicitly:

- `prefix_fragment`
  - A leading partial record that belongs to a previous chunk.
- `complete_rows`
  - Rows that are complete inside this chunk.
- `suffix_fragment`
  - A trailing partial record that must be joined with a later chunk.

The validator owns fragment stitching. That is where split rows become complete
rows before being released to `RowCollection`.

Serial parsing handles the same semantic problem through remainder/backtracking:

- `MmapParser` adjusts the next mmap offset.
- `StreamParser` stores leftover bytes and prepends them to the next read.

## Row Queue Handoff

Parsed rows are pushed into `RowCollection`.

- Threaded builds use `ThreadSafeDeque<CSVRow>`.
- No-thread builds alias the same queue name to `SingleThreadDeque<CSVRow>`.

Both queues satisfy the parser queue concept: push rows, append row batches, pop
rows, drain rows, and expose wait/notify hooks. Diagnostic helpers such as
`ThreadSafeDeque::inspect()` are intentionally not part of the shared queue
contract.

## From CSVRow to CSVField

`CSVRow` is a lightweight view over shared `RawCSVData`.

When the user asks for a field:

```cpp
auto field = row["name"];
auto value = field.get<std::string>();
```

the row looks up the field metadata, slices `RawCSVData::data`, and returns a
`CSVField`.

`CSVField` materializes only what is needed:

- Unquoted, untrimmed fields can remain a `string_view`.
- Quoted fields unescape doubled quotes when accessed.
- Trimmed fields apply trim behavior when accessed.
- Typed conversions happen in `get<T>()` / `try_get<T>()`.

This keeps parser throughput focused on boundary detection instead of string
construction.

## Where Copies Happen

Expected copies:

- Stream sources copy bytes into owned windows.
- `CSVField::get<std::string>()` returns an owning string.
- Quoted fields with escaped quotes may materialize an unescaped string.
- Some repair paths concatenate row fragments when speculation was wrong or a
  row spans chunks.

Expected non-copies:

- Mmap source bytes are viewed, not copied, during normal parsing.
- `CSVRow` copies are cheap shared ownership transfers.
- Field metadata records offsets and lengths, not strings.

## Common Bug Sites

Check these first when field data looks wrong:

- Mmap vs stream path parity.
- Chunk boundaries around fields larger than 10MB.
- CRLF split across chunks.
- Embedded newlines inside quoted fields.
- Wrong speculative initial quote state.
- Validator repair path and fragment concatenation.
- `RawCSVData` backing ownership lifetime.
- Lazy trim/unescape behavior in `CSVRow` / `CSVField`.
- Runtime `CSVFormat::threading(false)` path, which should change scheduling
  only, not row contents.

## Debugging Rule of Thumb

If the field boundary is wrong, start in `CSVParserCore` or the speculative
fragment/validator path.

If the boundary is right but the string value is wrong, start in `CSVRow`,
`CSVField`, or `RawCSVData` ownership/materialization.

If only large files fail, suspect chunk boundaries.

If only one constructor fails, suspect `MmapParser` or `StreamParser`.
