# Internal Architecture

This document describes the high-level architecture of the CSV parser internals and how major classes interact.

Scope:
- Internal component responsibilities
- End-to-end data flow
- Core invariants for safe changes
- Where to add tests for subsystem changes

For queue synchronization protocol details, see THREADSAFE_DEQUE_DESIGN.md.
For AI-agent workflow and guardrails, see ../../AGENTS.md and ../../tests/AGENTS.md.

## 1. System Shape

The parser is a streaming producer-consumer pipeline:

1. A parser implementation reads source bytes in chunks.
2. Parsed rows are emitted into a queue.
3. Consumer-side APIs expose rows and fields lazily.

Two independent parser paths exist and must be kept behaviorally aligned:

- File constructor path: memory-mapped parser
- iostream constructor path: stream parser

## 2. Major Components

### API-facing core

- CSVReader
  - Orchestrates parser lifecycle, worker cycle, and row retrieval.
  - Holds parser, queue, format, and exception propagation state.

- CSVRow
  - Lightweight row view over shared chunk data.
  - Resolves field slices and supports index/name access.

- CSVField
  - Field-level typed conversion facade.
  - Defers conversion work until requested.

- CSVFormat
  - Parse configuration (delimiter/quote/trim/header/chunk size/policies).

### Parsing core

- IBasicCSVParser
  - Shared parse loop and field/row state machine.

- MmapParser
  - Reads chunks from memory maps and handles chunk-transition remainder.

- StreamParser
  - Reads chunks from stream sources.

### Internal storage and transport

- RawCSVData
  - Shared chunk payload and per-chunk parse metadata.

- RawCSVFieldList
  - Compact field metadata storage (start/length/quote flags).

- ThreadSafeDeque<CSVRow>
  - Parser-to-consumer transport queue.
  - Synchronization protocol is documented in THREADSAFE_DEQUE_DESIGN.md.

### Relationship diagrams

Parser hierarchy:

```text
                  +------------------+
                  | IBasicCSVParser  |
                  | (abstract base)  |
                  +---------+--------+
                            ^
                 +----------+----------+
                 |                     |
        +--------+--------+    +-------+--------+
        |   MmapParser    |    |  StreamParser  |
        | concrete parser |    | concrete parser|
        +-----------------+    +----------------+
```

Reader + row/data ownership:

```text
CSVReader
  -> parser->next() builds RawCSVData chunk
  -> emits CSVRow objects into ThreadSafeDeque

         +--------------------------+
         | RawCSVData               |
         | - _data: shared_ptr<void>|
         | - data: string_view      |
         | - fields: RawCSVFieldList|
         +------------+-------------+
                      ^
                      | shared_ptr<RawCSVData>
          +-----------+-----------+-----------+
          |                       |           |
       CSVRow #1              CSVRow #2    CSVRow #N
```

Notes:
- Multiple CSVRow instances can share the same RawCSVData chunk.
- RawCSVData lifetime extends until the last referencing CSVRow is destroyed.
- RawCSVFieldList is contained inside RawCSVData and indexes slices into the backing data payload.

CSVRow -> CSVField lazy materialization:

```text
RawCSVData
  |- data (chunk bytes)
  |- fields[i] = {start, length, has_double_quote}
  v
CSVRow::get_field_impl(i)
  -> slice = data.substr(start, length)
  -> if quoted: unescape/cached materialization
  -> if trim enabled: apply trim at access time
  v
CSVField(string_view)
  -> typed conversion only when get<T>() / try_get<T>() is called
```

Implication:
- Parser throughput stays focused on boundary detection and row emission; expensive string work is deferred until fields are actually accessed.

## 3. End-to-End Flow

Source bytes -> parser chunk read -> parse loop -> RawCSVData + RawCSVFieldList -> CSVRow enqueue -> CSVReader read_row / iteration -> CSVField materialization

Operationally:

1. CSVReader starts a read cycle with current chunk size.
2. Parser next(bytes) ingests one chunk and emits complete rows.
3. Queue buffers rows for consumer-side retrieval.
4. CSVRow/CSVField lazily materialize trim/unescape/conversion behavior.
5. Worker completion and errors are signaled back to the consumer side.

## 4. Key Invariants

### Chunk boundary integrity

Fields spanning chunk boundaries must not be split/corrupted.

### Path parity

Mmap and stream parsers must preserve the same externally observable behavior.

### Lazy materialization contract

Trimming/unescaping/conversion behavior must remain coherent across parser and field-access layers.

### Bounded streaming semantics

Avoid designs that force retaining all parsed chunks globally.

## 5. Change Impact Map

- Parser state machine changes:
  - basic_csv_parser.hpp, basic_csv_parser.cpp

- Chunk transition changes:
  - basic_csv_parser.cpp (MmapParser/StreamParser next)

- Reader worker/iteration behavior:
  - csv_reader.hpp, csv_reader.cpp, csv_reader_iterator.cpp

- Field extraction and trimming/unescaping:
  - csv_row.hpp, csv_row.cpp, raw_csv_data.hpp

- Parse configuration behavior:
  - csv_format.hpp, csv_format.cpp

- Queue synchronization semantics:
  - thread_safe_deque.hpp, THREADSAFE_DEQUE_DESIGN.md

## 6. Test Guidance by Subsystem

For full testing strategy, checklist, and conventions, see:
- ../../tests/AGENTS.md
