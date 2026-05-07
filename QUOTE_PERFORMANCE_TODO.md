# Quoted Field Performance TODO

`csv-parser` is now very competitive on clean positional-read and ETL workloads,
but heavily quoted workloads still favor `fast-cpp-csv-parser` in the current
benchmarks. This document captures the likely causes and investigation plan.

## Current Hypothesis

The slowdown is probably less about quote detection itself and more about the
cost of making quoted fields usable after parsing.

`fast-cpp-csv-parser` has a cheaper quoted-field model because it:

- Parses one physical line at a time
- Does not support embedded quoted newlines
- Mutates the input line buffer in place
- Compacts doubled quotes directly into that mutable buffer
- Uses compile-time column and quote policies

`csv-parser` intentionally supports a broader model:

- RFC 4180 embedded newlines
- Chunk boundaries and speculative repair
- Runtime format configuration
- Dynamic `CSVRow` / `DataFrame` workflows
- Lazy field access through shared raw backing storage

That design is more flexible, but quote-heavy ETL workloads eventually touch
most fields anyway, so lazy quoted-field realization can become a large tax.

## Suspect 1: Lazy Quoted-Field Realization

Status: implemented in this branch. Doubled-quote fields are now realized by
parser workers into `RawCSVData::quote_arena`, an append-only stable-block arena,
and `RawCSVField` stores start/length plus a realized-storage flag. Plain fields,
including quoted fields without doubled quotes, still view the raw immutable
backing bytes.

Previous storage:

```cpp
std::unordered_map<size_t, std::string> double_quote_fields;
```

Previously, when a field contained doubled quotes, `CSVRow::get_field_impl()`:

1. Looks up the field index in `double_quote_fields`
2. Lazily builds an unescaped string if missing
3. Returns a `string_view` into the cached string

For quote-heavy materialization benchmarks that visit every field, this created
many small hash lookups and string-building operations. That is likely the
largest fixed cost in this pass.

Ideas to test:

- Add a benchmark that parses quoted CSV but does not access the quoted column
- Add a benchmark that accesses every field
- Compare those against DataFrame construction
- If the gap appears only on access, focus on realization

Completed fix:

- Realize only doubled-quote fields during `CSVParserCore::push_field()`
- Store realized bytes densely in stable blocks via `RawCSVData::quote_arena`
- Remove the lazy unordered-map and double-checked locking path
- Keep raw views for fields without doubled quotes

Future benchmark work:

- Add a benchmark that parses quoted CSV but does not access the quoted column
- Add a benchmark that accesses every field
- Compare those against DataFrame construction
- Measure the new parser-time work tradeoff for lazy-subset streaming users

## Suspect 2: RawCSVField Size

Previous field metadata was roughly:

```cpp
size_t start;
size_t length;
bool has_double_quote;
```

Current field metadata stores chunk-local 32-bit offsets:

```cpp
uint32_t start;
uint32_t length;
bool is_realized;
```

`is_realized == false` means field access returns a view into the raw backing
data with row-relative `start`. Otherwise `start/length` refer to
`RawCSVData::quote_arena`.

On 64-bit builds, this likely occupies 24 bytes after padding. For large
materialized inputs, field metadata alone can become huge.

Example: 5M rows x 8 columns = 40M fields.

At 24 bytes per field, field metadata is roughly 960 MB before accounting for
rows, raw data, edit overlays, string caches, or container overhead.

Ideas to test:

- Replace `size_t` with `uint32_t` for chunk-local start/length
- Store flags in a compact byte
- Measure `sizeof(RawCSVField)` before and after
- Add a static assertion documenting the intended size once settled

Design note:

Chunk-local offsets should fit comfortably in 32 bits because parser chunks are
bounded. Any future very-large-row path must preserve correctness before
shrinking this structure.

## Known Design: RawCSVFieldList Paged Blocks

Current field storage is paged:

```cpp
std::vector<std::unique_ptr<RawCSVField[]>> _owned_blocks;
```

Field lookup uses division/modulo to find the block and offset. At first glance,
a contiguous `std::vector<RawCSVField>` looks tempting for sequential scans, but
this parser already tried that shape and allocator pressure was much worse.

Do not casually replace the paged design with a plain vector.

Why the current design exists:

- Field count per chunk varies with row shape, chunk size, malformed rows,
  comments, multiline rows, and payload profile
- Exact pre-reservation is hard to guarantee
- A growing vector can repeatedly reallocate and move large field metadata
- Previous vector-backed implementations produced excessive `malloc()` pressure
- The paged structure keeps append cost stable and avoids invalidating storage

The division/modulo and one extra pointer indirection on field access are likely
much cheaper than allocator churn during parse/materialization. Treat the paged
layout as intentional prior art unless profiling proves otherwise.

Possible future work:

- Add a comment near `RawCSVFieldList` explaining why it is paged
- If revisiting this, require allocator/malloc profiling, not just throughput
- Consider changing block size or growth policy before replacing the structure

## Suspect 4: Parser DFA Cost

The DFA does more work than `fast-cpp-csv-parser` because it must preserve full
CSV correctness across chunks and embedded newlines. Some of that cost is
intentional and should not be optimized away.

Before touching the DFA, isolate whether the bottleneck is parsing or field
realization:

- Parse quoted data and count rows only
- Parse quoted data and access no quoted fields
- Parse quoted data and access only the quoted column
- Parse quoted data and materialize every field

If the cost appears only after field access, do not start with DFA surgery.

## Preferred Direction

The unordered-map replacement is complete. The default streaming path now does
slightly more parser-thread work only for fields that actually contain doubled
quotes, while ordinary fields and plain quoted fields remain raw-backed views.

Remaining likely work:

- Shrink field metadata only after correctness and benchmark coverage are in
  place
- Rerun quoted/clean benchmarks before making more parser DFA changes

## Benchmark Checklist

Before and after any implementation, rerun:

- `csv_parser_fast_cpp_read_bench`
- `csv_parser_multi_pass_bench`
- `dataframe_rapidcsv_roundtrip_bench`
- At least clean and quoted profiles
- 500K and 5M row counts

Watch for:

- Clean-data regression
- Quoted-data improvement
- DataFrame construction scaling
- Memory growth
- Single-thread and multi-thread behavior
