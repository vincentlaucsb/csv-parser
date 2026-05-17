@page internal_architecture Internal Architecture

# Internal Architecture

This document describes the high-level architecture of the CSV parser internals and how major classes interact.

Scope:
- Internal component responsibilities
- End-to-end data flow
- Core invariants for safe changes
- Where to add tests for subsystem changes

For queue synchronization protocol details, see THREADSAFE_DEQUE_DESIGN.md.
For a narrative byte-to-field walkthrough, see JOURNEY_OF_A_CSV_FIELD.md.
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

- CSVReadScheduler
  - Internal concrete scheduler selected from sync/thread-capable implementations.
  - Owns worker-thread launch/join and exception transfer so CSVReader does not
    intermix public facade logic with compile-time threading branches.

- CSVRow
  - Lightweight row view over shared chunk data.
  - Resolves field slices and supports index/name access.

- CSVField
  - Field-level typed conversion facade.
  - Defers conversion work until requested.

- CSVFormat
  - Parse configuration (delimiter/quote/trim/header/chunk size/policies).
  - Runtime threading can be disabled per reader with `CSVFormat::threading(false)`.

- DataFrame
  - Batch-oriented facade over `CSVRow` objects with keyed access, sparse edit
    overlays, column views, and row-like writer compatibility.
  - Public types remain in namespace `csv`, while implementation headers are
    split under `include/internal/data_frame/` to keep each proxy/helper focused.

### Parsing core

- CSVParserCore
  - Templated, non-virtual byte parser core in parser/core.hpp.
  - Owns DFA state, BOM handling, field/row construction, and concrete row-sink emission.
  - Source adapters feed byte windows into it; it does not own file, mmap, or stream source mechanics.

- PermissiveParsePolicy
  - No-op parse policy extension point.
  - Preserves RawCSVData/CSVRow view-based field access while keeping the hot path free of virtual row sinks.

- CSVParserDriverBase
  - Internal source-adapter base that preserves the parser driver API used by CSVReader.
  - Delegates byte parsing to CSVParserCore.
  - Lives under `csv::internals::parser`; files in `include/internal/parser/`
    intentionally share that namespace.

- speculative/chunk_parser.hpp
  - Compatibility include for speculative chunk helpers.

- speculative/chunks.hpp
  - Row-fragment repair primitives and chunk parser shell used by speculative parsing.

- speculative/scanner.hpp, speculative/validator.hpp, speculative/parallel_parser.hpp
  - Speculative scanner, row-fragment validation/repair, and optional threaded chunk parser.
  - Speculative-only helpers live under `csv::internals::speculative`.
  - Compiled out when `CSV_ENABLE_THREADS=0`.

- parser/orchestrator.hpp
  - Chooses serial CSVParserCore parsing or speculative parallel parsing for a byte window.

- MmapParser
  - Reads chunks from memory maps and handles chunk-transition remainder.
  - Declared in parser/mmap.hpp; implemented in parser/mmap.cpp.

- StreamParser
  - Reads chunks from stream sources.
  - Template definition lives in parser/stream.hpp.

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
                  +----------------------+
                  |   CSVParserCore      |
                  | byte parser state    |
                  +----------+-----------+
                             ^
                  +----------+-----------+
                  | CSVParserDriverBase  |
                  | source adapter base  |
                  +----------+-----------+
                             ^
                 +-----------+---------+
                 |                     |
        +--------+--------+    +-------+--------+
        |   MmapParser    |    |  StreamParser  |
        | concrete source |    | concrete source|
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

CSVRow -> CSVField field access:

```text
RawCSVData
  |- data (chunk bytes)
  |- quote_arena (stable sidecar bytes for doubled-quote fields)
  |- fields[i] = {start, length, is_realized}
  v
CSVRow::get_field_impl(i)
  -> if is_realized: slice = quote_arena.view(start, length)
  -> otherwise: slice = data.substr(start, length)
  -> if trim enabled: apply trim at access time
  v
CSVField(string_view)
  -> typed conversion only when get<T>() / try_get<T>() is called
```

Implication:
- Raw source bytes stay immutable and ordinary fields remain zero-copy views.
- Fields containing doubled quotes are realized once by the parser into packed sidecar storage, avoiding per-access hash lookups and lazy string construction.

## 3. End-to-End Flow

Source bytes -> parser chunk read -> parse loop -> RawCSVData + RawCSVFieldList -> CSVRow enqueue -> CSVReader read_row / iteration -> CSVField access/conversion

Operationally:

1. CSVReader starts a read cycle with current chunk size.
2. Parser next(bytes) ingests one chunk and emits complete rows.
3. Queue buffers rows for consumer-side retrieval.
4. CSVRow applies trim when creating field views, and CSVField lazily performs
   scalar classification and typed conversion.
5. CSVReadScheduler signals worker completion and transfers errors back to the
   consumer side. When `CSVFormat::threading(false)` is active, the same parse
   cycle runs synchronously on the caller thread and speculative parsing is
   disabled. In thread-enabled builds this runtime opt-out still uses
   `ThreadSafeDeque<CSVRow>` internally; replacing it with `SingleThreadDeque`
   would be a small optimization, not a semantic difference.

## 4. Key Invariants

### Internal folders map to internal namespaces

When a subsystem earns a folder under `include/internal/`, its namespace should
follow the folder path. For example, parser source adapters live in
`include/internal/parser/` and `csv::internals::parser`, while speculative
helpers live in `include/internal/speculative/` and
`csv::internals::speculative`.

### Chunk boundary integrity

Fields spanning chunk boundaries must not be split/corrupted.

### Path parity

Mmap and stream parsers must preserve the same externally observable behavior.

### Field storage and conversion contract

The parser realizes doubled-quote fields into `RawCSVData::quote_arena`; ordinary
fields remain views into source bytes. `CSVRow` applies trimming when creating a
field view. `CSVField` owns scalar classification and typed conversion caching.

### Bounded streaming semantics

Avoid designs that force retaining all parsed chunks globally.

### CSVReader::iterator is single-pass by design

`CSVReader::iterator` carries `std::input_iterator_tag` intentionally — this is a hard architectural constraint, not an oversight:

- Rows are backed by `RawCSVData` chunks that are freed as the iterator advances.
- Promoting to `ForwardIterator` would require retaining every chunk for the lifetime of any copy of the iterator, which means a 50 GB CSV would require 50+ GB of resident memory — defeating the entire streaming architecture.
- Algorithms that require `ForwardIterator` (`std::max_element`, `std::sort`, etc.) may appear to work on small files (where only one chunk is ever allocated) but are unsafe in general: accessing an earlier iterator position after the chunk it pointed into has been freed is undefined behavior.

**Correct pattern when random-access algorithms are needed:**
```cpp
std::vector<csv::CSVRow> rows(reader.begin(), reader.end());
auto it = std::max_element(rows.begin(), rows.end(), cmp);
```

**What NOT to do:**
- Do not add a `std::vector<RawCSVDataPtr>` cache to `CSVReader::iterator` to support multi-pass. That destroys bounded-memory behavior.
- Do not change `iterator_category` to `forward_iterator_tag` without first solving the chunk-lifetime problem.

This invariant is canonical here and summarized in the root `AGENTS.md` guidance.

## 5. Change Impact Map

- Parser state machine changes:
  - parser/core.hpp, speculative/chunks.hpp

- Chunk transition changes:
  - parser/mmap.cpp (MmapParser next), parser/stream.hpp (StreamParser next)

- Speculative parallel parsing changes:
  - speculative/scanner.hpp, speculative/validator.hpp, speculative/parallel_parser.hpp, parser/orchestrator.hpp, speculative/diagnostics.hpp, parser/mmap.cpp, parser/stream.hpp

- Reader worker/iteration behavior:
  - csv_reader.hpp, csv_reader.cpp, csv_reader_iterator.cpp, parser/scheduler.hpp

- Field extraction, backing storage, and trimming/unescaping:
  - csv_row.hpp, csv_row.cpp, raw_csv_data.hpp, memory/*.hpp

- Parse configuration behavior:
  - csv_format.hpp, csv_format.cpp

- Queue synchronization semantics:
  - thread_safe_deque.hpp, THREADSAFE_DEQUE_DESIGN.md

## 6. Test Guidance by Subsystem

For full testing strategy, checklist, and conventions, see:
- ../../tests/AGENTS.md
