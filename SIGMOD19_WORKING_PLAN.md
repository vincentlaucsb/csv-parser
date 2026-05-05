# SIGMOD 2019 Speculative Parallel Parsing - Working Plan

Temporary design and implementation scratchpad for adding the SIGMOD 2019
speculative parallel CSV parsing algorithm as a minimal extension to the
existing parser.

## Goals

- Add optional speculative parallel parsing for large CSV inputs
- Keep the existing single-threaded path as the default path with zero overhead
- Treat CSV sourcing as orthogonal to parsing logic
- Extract the current DFA-heavy parse loop into a testable parser core
- Keep the implementation clean, readable, and minimal

## Agreed Design Constraints

1. The extracted parser core is the parser.
   - Speculative mode does not introduce a second parser.
   - Serial and speculative drivers both use the same parse core.

2. CSV source handling is orthogonal.
   - Mmap vs stream is a driver concern, not a parser-core concern.

3. Seed state may be richer than a single boolean.
   - Prefer clarity and correctness over aggressively minimizing the seed
     object.

4. Internal API should favor testability.
   - Push-based row emission is preferred if it makes unit testing cleaner.

5. Validator owns release of rows.
   - Workers may produce speculative parse results.
   - Validator decides what is safe to publish.
   - Validator also owns repair/fallback.

6. Repair should do the minimal work needed for correctness.
   - On validation failure, repair the smallest affected chunk region.

7. Repair logic must be heavily tested.
   - Especially with intentionally wrong chunk state metadata.

## Proposed Architecture

### 1. Parse core

Source-agnostic parser unit extracted from `IBasicCSVParser`.

Responsibilities:
- consume a chunk/span
- accept an explicit initial parser state
- emit rows to a sink/callback
- report ending parser state
- report remainder/carry metadata

Non-goals:
- no direct thread orchestration
- no direct row publication to the public queue

### 2. Scanner / speculation stage

Cheap prefix-based scanner that predicts likely initial quoted-state for each
chunk based on a small prefix (per SIGMOD 2019).

### 3. Worker stage

Each worker parses one chunk speculatively using:
- the chunk payload
- assumed seed state
- shared parse core

Workers return parsed chunk results; they do not publish rows directly.

### 4. Validator stage

Validator:
- receives chunk results in sequence order
- checks boundary compatibility between adjacent chunks
- publishes only validated rows
- invokes repair on mismatch

### 5. Repair stage

Repair should likely be a free function.

Responsibilities:
- rerun parse on the failed chunk using corrected seed state
- replace speculative rows with repaired rows
- preserve row order

## Suggested Internal Types

These are placeholders, not final API.

```cpp
struct ParserSeedState {
    bool in_quoted_field = false;
    bool quote_escape = false;
};

struct ParsedChunkResult {
    size_t sequence_number = 0;
    ParserSeedState assumed_start_state;
    ParserSeedState actual_end_state;
    size_t parsed_bytes = 0;
    size_t remainder_offset = 0;
    std::vector<CSVRow> rows;
};
```

## Testing Priorities

### Parser-core unit tests

- seeded start outside quoted field
- seeded start inside quoted field
- escaped quote handling across chunk boundaries
- ending state reporting
- remainder extraction correctness

### Validator tests

- valid adjacent chunk state passes
- invalid adjacent chunk state is detected
- rows are not released before validation
- repaired rows replace speculative rows correctly
- global row order is preserved

### Repair tests

The key shape:
- construct chunk results with intentionally wrong state metadata
- feed them to validator
- verify validator triggers repair
- verify repaired output is correct

### Regression tests

- quoted multiline rows split across chunk boundaries
- chunks with embedded escaped quotes near boundaries
- large-file speculative mode auto-enable/disable behavior

## TODO

- [x] Identify the smallest clean extraction boundary inside `IBasicCSVParser`
- [x] Define the parser-core input/output contract
- [x] Define the real seed/end state from the existing DFA
- [x] Extract parse core without changing serial behavior
- [x] Add parser-core unit tests before speculative orchestration
- [x] Implement `ChunkSpeculation` struct
- [x] Implement `SpeculativeScanner`
- [x] Add row-fragment representation for validator-owned boundary rows
- [x] Implement `ParsedChunkResult`
- [x] Implement validator with ordered release
- [x] Implement repair free function
- [x] Add intentionally wrong-state validator/repair tests
- [x] Implement source-agnostic parallel chunk orchestrator
- [x] Add config/public API knob for speculative mode
- [x] Auto-disable on small files / low thread count
- [x] Wire speculative mode into filename-backed reader
- [x] Verify zero overhead for normal serial path

## Prompt Seed

Use this if the conversation context gets compacted:

> We are adding the SIGMOD 2019 speculative parallel CSV parsing algorithm as a
> minimal extension to `csv-parser`.
>
> Important design decisions already made:
> - The extracted parse core is the parser. There must not be separate serial
>   and speculative parsers.
> - CSV sourcing is orthogonal to parse logic.
> - Internal APIs should prioritize testability and clarity.
> - Push-based row emission is preferred.
> - Validator owns release of rows and also owns repair/fallback.
> - Repair should be implemented as a free function and tested aggressively by
>   feeding intentionally wrong chunk-state metadata into the validator.
> - Do the minimal work necessary on validation failure to restore correctness.
>
> Current task: validate the filename-backed speculative path and keep the
> default serial path unchanged for normal readers.
