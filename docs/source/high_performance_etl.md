@page high_performance_etl High Performance ETL

# High Performance ETL

This page covers the highest-leverage ETL-oriented APIs in `csv-parser`:

- `csv::CSVReader::read_chunk()`
- `csv::DataFrame`
- `csv::DataFrameExecutor`
- `csv::chunk_parallel_apply()`

The library supports both low-level batch control and a higher-level chunked
parallel helper. Which one you choose mainly depends on whether you need to
mutate a batch before analysis.

## Two Common ETL Shapes

### 1. Read chunk -> build a batch `DataFrame` -> edit -> analyze

This is the most flexible path.

Use it when you want to:

- keep memory bounded
- apply sparse edits to a batch before analysis
- perform row-wise ETL tasks such as null-ish value coercion
- run `group_by()`, `column()`, or `column_parallel_apply()` on just the rows
  in the current chunk

\snippet tests/test_data_frame_etl.cpp High Performance ETL Batch Bridge Example

The example above shows a very common ETL shape:

- read a bounded chunk from an in-memory or file-backed source
- apply sparse overlay edits to normalize selected cells
- normalize null-ish values to empty strings
- schedule work only for the columns that matter
- keep the result in explicit worker-owned state

This pattern works well because `DataFrame` is a short-lived batch bridge:

- `CSVReader` streams rows into a caller-owned `std::vector<CSVRow>`
- `DataFrame` wraps that batch
- sparse overlay edits are applied only where needed
- parallel analysis runs against the edited view
- the batch is discarded before the next chunk

That design avoids many of the lifetime and aliasing problems that show up when
parallelism is layered onto a long-lived mutable table.

### 2. Use `chunk_parallel_apply()` directly

This is the common-case helper when you do not need to mutate each batch before
processing.

`chunk_parallel_apply()`:

- reads from `CSVReader` in bounded chunks
- wraps each chunk in a temporary `DataFrame`
- runs a per-column callback using `DataFrameExecutor`
- can either accumulate results in one explicit state object per column or let
  you manage output storage externally via `DataFrameColumn::index()`
- can target all columns or just a selected subset by column index

This is usually the shortest path for:

- schema inference
- column summaries
- frequency counts
- per-column aggregation passes

## Choosing Between `read_chunk()` and `chunk_parallel_apply()`

Use `read_chunk()` when:

- you need to edit the batch before analysis
- you want to filter or promote only some rows into a `DataFrame`
- you need custom batch-level orchestration

Use `chunk_parallel_apply()` when:

- the chunk can be treated as read-only
- you want the simplest path to chunked parallel column processing
- one state object per column is a natural fit for the workload

## Thread-Safety Notes

`DataFrameExecutor` is designed around batch-scoped `DataFrame` objects, which
keeps the parallel story much cleaner than a long-lived shared table.

Safe patterns:

- reading from the provided `column` view
- reading from explicit references to batch state captured by the caller
- updating only the worker's own per-column state object
- applying sparse-overlay cell edits through `DataFrameRow` / `DataFrameCell`
  when workers are updating row data in place

Unsafe pattern:

- structural mutation during parallel work, such as `erase()`
- relying on conflicting concurrent writes to the same cells unless
  last-write-wins behavior is acceptable

For simple cell updates, the row-local overlay lock is there to make rare
collisions boring rather than catastrophic. For structural changes, stay on the
caller thread.

## Why This Matters

Many ETL workflows fundamentally look like this:

1. read a chunk
2. materialize usable row objects
3. perform a small number of transformations or edits
4. aggregate or emit

`csv-parser` is optimized for exactly that shape. It is not just a parser; it
also provides the batch-bridge and parallel column-processing pieces that would
otherwise need to be hand-rolled on top of a lower-level CSV reader.
