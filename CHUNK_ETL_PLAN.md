# Chunked ETL Plan

Detailed implementation plan for evolving `CSVReader` and `DataFrame` toward
streaming, chunk-oriented ETL workflows without turning the library into a
heavyweight dataframe engine.

## Working Status

- [x] `CSVReader::read_chunk(std::vector<CSVRow>&, size_t)`
- [x] `DataFrame(std::vector<CSVRow>)`
- [x] `DataFrame::swap_rows(std::vector<CSVRow>&)`
- [x] `DataFrameExecutor`
- [x] `DataFrame::column_parallel_apply(...)`
- [x] `chunk_parallel_apply(...)` convenience helper
- [x] Reimplement `csv_data_types()` on top of `DataFrameExecutor`
- [x] Remove the rest of `CSVStat`

## Goals

1. Add `CSVReader::read_chunk(...)` as a low-level primitive for bounded-memory
   batch processing.
2. Add a persistent, reusable batch-analysis context for `DataFrame` so column
   analysis can be parallelized across chunks without respawning worker threads.
3. Keep `DataFrame` lightweight and useful as a CSV-native bridge object:
   - sparse edits
   - grouping
   - column extraction/vectorization
   - JSON/write-back
   - future SQL/schema-inference helpers
4. Preserve an escape hatch for advanced users who want to stay at the
   `CSVReader` + `std::vector<CSVRow>` level.

## Non-Goals

- Turning `DataFrame` into a full general-purpose dataframe engine
- Adding arbitrary column insertion/removal at this stage
- Baking schema inference, SQL loading, or column statistics directly into
  `CSVReader`
- Requiring users to adopt `DataFrame` if they prefer lower-level chunk access

## Design Summary

### 1. `CSVReader::read_chunk(...)`

`CSVReader` should expose a chunk-reading primitive that reuses caller-owned
storage and returns rows in bounded batches.

Proposed baseline API:

```cpp
bool read_chunk(std::vector<CSVRow>& out, size_t max_rows);
```

Semantics:

- Clears `out` before filling it with up to `max_rows` rows
- Returns `true` if any rows were produced
- Returns `false` only when end-of-stream is reached and no rows were produced
- Final partial chunk still returns `true`
- Leaves row contents valid after the call via existing `CSVRow` ownership rules

Why this shape:

- Reuses storage across chunks
- Easy to understand
- Works for mmap and stream parser paths
- Keeps the primitive orthogonal to any higher-level `DataFrame` features

Possible future convenience overloads:

```cpp
std::vector<CSVRow> read_chunk(size_t max_rows);
```

But the vector-reuse overload should remain the primary API.

### 2. `DataFrame` as the Chunk Bridge

`DataFrame` should be treated as the higher-level batch abstraction layered on
top of chunks, not as a full table engine.

This is a good fit because `DataFrame` already provides:

- sparse cell edits
- keyed or positional access
- group-by helpers
- column extraction
- write-back to CSV
- JSON export

The current role becomes clearer:

> `CSVReader` streams rows; `DataFrame` turns a chunk of rows into an analyzable,
> editable batch.

### 3. Persistent Parallel Context

Column-parallel algorithms should not respawn worker threads for every chunk.
Instead, use a persistent context object that owns thread lifetime and wakes
workers for each new `DataFrame` batch.

Chosen working name:

- `DataFrameExecutor`

Reasoning:

- "Context" clearly signals reusable state and thread lifetime
- it stays broad enough for schema inference and future ETL helpers
- it is shorter and easier to explain in docs than more elaborate variants

### 4. Column-Parallel Entry Point

`DataFrame` should expose a column-parallel batch operation that uses the
persistent context.

Current rough idea:

```cpp
df.column_parallel_apply(context, states, fn);
```

Where:

- `context` owns worker threads and scheduling/wakeup logic
- `states` is one object per column, owned by the caller
- `fn` updates each per-column state using the current batch

This allows repeated chunk workflows like:

```cpp
std::vector<CSVRow> rows;
csv::DataFrame<> batch;
csv::DataFrameExecutor exec;
std::vector<ColumnInferenceState> states;

while (reader.read_chunk(rows, 50000)) {
    batch.swap_rows(rows);
    batch.column_parallel_apply(exec, states, infer_column);
}
```

## Why `DataFrame` Instead of a Generic Orchestrator?

We considered a generic column-parallel orchestrator over arbitrary collections
of `CSVRow`, but that turns out to be a poor fit.

Reasons:

1. A generic orchestrator would not naturally understand visible edits.
2. It would not naturally support transformation-oriented workflows.
3. It would need awkward abstraction just to recover features `DataFrame`
   already has.
4. A "generic" API without edit/vectorization semantics would be seriously
   weakened for the ETL and schema-inference use cases we care about.

Conclusion:

- `CSVReader::read_chunk()` remains the low-level escape hatch
- `DataFrame` remains the default higher-level batch abstraction
- persistent compute orchestration should be designed around `DataFrame`

## Proposed `DataFrame` Additions

### A. Construct from Existing Rows

`DataFrame` should support direct construction from a batch of rows.

Proposed APIs:

```cpp
explicit DataFrame(std::vector<CSVRow> rows);
explicit DataFrame(std::vector<CSVRow> rows, const DataFrameOptions& options);
```

Open questions:

- Should column metadata come from the first row?
- Do we require non-empty input for keyed construction?
- Do we preserve current duplicate-key handling semantics via `DataFrameOptions`?

Recommendation:

- Start with unkeyed construction from `std::vector<CSVRow>`
- Add keyed/options-based construction only when the low-level semantics are
  nailed down

### B. Reuse a `DataFrame` Shell Across Chunks

Add a row-swap/reset operation so one `DataFrame` can be reused across many
chunks without reconstruction overhead.

Proposed baseline API:

```cpp
void swap_rows(std::vector<CSVRow>& rows);
```

Required semantics:

- Swaps in the new row batch
- Clears sparse edits
- Invalidates key index
- Invalidates any cached JSON conversion state if column metadata changes
- Preserves lightweight `DataFrame` object reuse across chunks

Important note:

Structural reset must invalidate outstanding row/cell proxies.

### C. Column-Parallel Compute API

This should be designed for batch analysis workloads such as:

- SQL schema inference
- candidate key detection
- duplicate detection
- nullability inference
- type confidence scoring
- enum/set extraction

Recommended API direction:

```cpp
template<typename State, typename Fn>
void column_parallel_apply(
    DataFrameExecutor& executor,
    std::vector<State>& states,
    Fn&& fn
) const;
```

Where `fn` conceptually operates on:

- column index
- visible column values in the current batch
- mutable per-column state

Possible callback shapes:

```cpp
fn(size_t column_index, const DataFrame& frame, State& state);
```

or

```cpp
fn(size_t column_index, csv::string_view value, State& state);
```

Recommendation:

- Start with the coarser per-column callback:

```cpp
fn(size_t column_index, const DataFrame& frame, State& state)
```

This keeps scheduling simple and lets the callback decide whether to scan by
rows, materialize a column vector, or use future helpers.

## Relationship to Existing `column()`

`column()` remains useful.

Even though it performs conversion/materialization, that cost is often worth it
for SQL/ETL workloads because:

- schema inference already requires typed conversion
- a materialized column improves cache locality
- repeated passes over a converted column are cheaper than repeated row hops

So the current mental model becomes:

- row iteration: cheap general access
- `column<T>()`: explicit materialization for column-oriented work

Future helpers that may fit this model:

- `map_column(...)`
- `column_set<T>()`
- `unique_column<T>()`
- `column_stats<T>()`

These are not prerequisites for the chunking/context plan.

## Implementation Order

### Phase 1: `CSVReader::read_chunk()`

Status: done

Tasks:

1. Add `read_chunk(std::vector<CSVRow>&, size_t)` to `CSVReader`
2. Ensure it works for both mmap and stream parser paths
3. Add tests for:
   - empty input
   - partial final chunk
   - exact chunk multiple
   - both parser paths
   - large files crossing the 10MB worker chunk boundary

### Phase 2: `DataFrame` over Existing Row Batches

Status: in progress

Tasks:

1. Add unkeyed `DataFrame(std::vector<CSVRow>)` -- done
2. Add `swap_rows(std::vector<CSVRow>&)` -- done
3. Define invalidation/reset behavior clearly -- done for batch replacement
4. Add tests for:
   - construction from row vector -- done
   - swap reuse -- done
   - edits cleared after swap -- done
   - row/cell proxy invalidation expectations

### Phase 3: `DataFrameExecutor`

Status: done

Tasks:

1. Create persistent worker context type -- done
2. Keep worker lifecycle out of `DataFrame` itself -- done
3. Define wake/sleep protocol for repeated chunk execution -- done
4. Add focused tests for:
   - repeated chunk processing
   - correctness across multiple batches
   - clean shutdown -- done indirectly via executor destruction path
   - deterministic state updates -- done for a single-batch application path

### Phase 4: `column_parallel_apply(...)`

Status: done

Tasks:

1. Add initial column-parallel execution API to `DataFrame` -- done
2. Wire it to `DataFrameExecutor` -- done
3. Validate with a realistic schema-inference example
4. Benchmark against single-threaded batch analysis

## Key Risks

1. **Threading complexity creep**
   - Risk: `DataFrame` becomes a thread host instead of a batch object
   - Mitigation: keep thread ownership in `DataFrameExecutor`

2. **Proxy invalidation confusion**
   - Risk: `swap_rows()` makes old row/cell proxies dangerous
   - Mitigation: document structural reset invalidation clearly

3. **Over-generalized callback API**
   - Risk: trying to support every possible orchestration style
   - Mitigation: start with a narrow SQL/ETL-oriented shape

4. **Chunk semantics divergence between mmap and stream**
   - Risk: one path behaves differently
   - Mitigation: test both paths explicitly

## Why This Fits the Library

This plan matches the library's character better than trying to build a full
dataframe system.

It preserves the library's strengths:

- fast CSV parsing
- lightweight `CSVRow`
- pragmatic ETL helpers
- easy downstream bridging

And it gives `DataFrame` a sharper identity:

> `DataFrame` is the batch bridge for CSV ETL workflows.

That is a meaningful role even if users eventually load the chunk into a more
capable dataframe library downstream.

## Current Recommendation

Proceed with:

1. `CSVReader::read_chunk(...)`
2. `DataFrame(std::vector<CSVRow>)`
3. `DataFrame::swap_rows(...)`
4. `DataFrameExecutor`
5. `DataFrame::column_parallel_apply(...)`

Do **not** pursue:

- true arbitrary-column dataframe semantics
- a generic row-collection parallel orchestrator
- heavy structural mutation features

until real user demand appears.

## Release Framing

This work likely fits best as a `4.0.0` release.

Why:

- the `DataFrame` API has already undergone meaningful cleanup and breaking
  simplification
- `CSVStat` is a candidate for removal rather than preservation
- the library direction is becoming clearer: parser-first, ETL-friendly,
  chunk/batch-oriented, and less burdened by specialized legacy helpers

Recommended `4.0.0` framing:

- ship the cleaned-up `DataFrame` API
- remove `CSVStat`
- retain `csv_data_types()` as a supported SQL/schema helper, but reimplement it
  on top of `DataFrameExecutor` instead of `CSVStat`
- document `CSVReader`, `CSVRow`, and `DataFrame` as the core composable ETL
  surface
- position future chunk/batch helpers (`read_chunk`, compute context, column
  parallel batch analysis) as the next natural extension of the library

Important note:

- `CSVStat` should not be removed without a replacement story in docs/release
  notes
- the replacement story is not "nothing"; it is:
  - `csv_data_types()` remains supported for SQL/schema-oriented workflows
  - the rest of `CSVStat` is retired in favor of lower-level composable
    primitives and chunk/batch workflows
  - the old `CSVStat`-style routines can live in unit tests as copy-pasteable
    reference implementations built on `read_chunk()`
