# CSV Parser Comparison Benchmarks

This directory is intentionally standalone. It is not referenced by the
top-level build, so comparison benchmarking remains opt-in and cannot slow
normal developer or CI builds.

## Executive Summary

These benchmarks were run on:

- Windows 11 Home 25H2 (`OS build 26200.8246`)
- Visual Studio 2026 / MSVC 19.50
- Windows SDK `10.0.26100.0`
- C++20
- AVX2 enabled
- `CSV_ENABLE_THREADS=ON`
- Google Benchmark 1.8.5
- 12th Gen Intel(R) Core(TM) i5-12400
- 12 logical CPUs at ~2.5 GHz
- Samsung 990 EVO SSD storage

The short version:

- `fast-cpp-csv-parser` remains a strong single-parser-worker baseline,
  especially on smaller raw read loops.
- With positional access and 4+ parser workers, `csv-parser` overtakes
  `fast-cpp-csv-parser` on clean and quoted read workloads.
- `csv-parser` beats Glaze's CSV reader on the mixed string/numeric/quoted
  workloads shown below, including the no-background-thread path.
- `csv-parser` wins the materialization and materialize+aggregation ETL
  benchmarks shown below, while also supporting multiline CSV that
  `fast-cpp-csv-parser` cannot parse.
- `csv-parser` beats `rapidcsv` for both DataFrame load and edited load+save
  workflows at 500K and 5M rows.

All tables below use **median real time** from the Google Benchmark output.

## Scope

- `csv_parser_read_bench`: read benchmarks for this library, using the filename
  constructor and therefore the native mmap parser where supported.
- `csv_parser_multi_pass_bench`: single-parser-worker materialization and
  multi-pass ETL-style benchmarks using reusable `CSVRow` objects.
- `csv_parser_fast_cpp_read_bench`: one-binary positional-read comparison
  between this library, `fast-cpp-csv-parser`, and Glaze. It labels scheduling
  explicitly: `csv-parser` no-background-thread, `csv-parser` SPSC
  producer/consumer, `csv-parser` speculative parallel parsing, and
  `fast-cpp-csv-parser` SPSC-style background I/O.
- `fast_cpp_csv_parser_read_bench`: read-focused benchmarks for
  `fast-cpp-csv-parser`.
- `fast_cpp_csv_parser_multi_pass_bench`: single-parser-worker materialization
  and multi-pass ETL-style benchmarks for `fast-cpp-csv-parser`, materializing
  into named-field row structs before running repeated passes.
- `glaze_csv_read_bench`: read-focused benchmarks for Glaze's CSV reader,
  materializing into reflected row structs through `glz::read<glz::opts_csv>`.
- `glaze_csv_multi_pass_bench`: single-parser-worker materialization and
  multi-pass ETL-style benchmarks for Glaze, using reflected row structs with
  native numeric deserialization for the aggregation fields.
- `dataframe_rapidcsv_roundtrip_bench`: table/round-trip-oriented benchmarks
  comparing this library's `DataFrame` workflow with `rapidcsv`, including
  load-only and edited full round-trip cases.

The rapidcsv round-trip benchmark uses `rapidcsv::Document::Save()` for the
rapidcsv write side. The csv-parser benchmark writes through this library's
`CSVWriter`, so benchmark names call that out explicitly.

## Configure

From the repository root:

```powershell
cmake -S benchmarks -B build/benchmarks -DCMAKE_BUILD_TYPE=Release
cmake --build build/benchmarks --config Release
```

By default, CMake uses `FetchContent` for Google Benchmark and the comparison
libraries if they are not already available. For offline or pinned dependency
work, provide local include directories instead:

```powershell
cmake -S benchmarks -B build/benchmarks `
  -DFAST_CPP_CSV_PARSER_INCLUDE_DIR=C:/src/fast-cpp-csv-parser `
  -DGLAZE_INCLUDE_DIR=C:/src/glaze/include `
  -DRAPIDCSV_INCLUDE_DIR=C:/src/rapidcsv/src
```

The Glaze benchmark targets are compiled with C++23 flags because current Glaze
headers use C++23 library facilities. The rest of the benchmark project remains
configured as C++20.

## Datasets

The benchmark programs expect a CSV file with this 8-column schema:

```text
id,city,state,category,amount,quantity,flag,note
```

Three payload profiles are supported:

- `clean`: deterministic scalar fields with no embedded commas, quotes, or
  line breaks.
- `quoted`: quoted commas and doubled quotes in the `note` column, but no
  embedded line breaks.
- `multiline`: quoted commas, doubled quotes, and embedded line breaks in the
  `note` column.

Two row shapes are supported:

- `standard`: the original compact schema values
- `wide`: longer string values across the same 8 columns

Use enough rows to cross csv-parser's 10 MB chunk boundary when measuring
reader behavior. At least 500K rows is the project baseline for large-file
coverage.

## How To Run

Example run:

```powershell
build/benchmarks/Release/csv_parser_read_bench.exe --benchmark_format=json data/bench_8col_500k.csv
build/benchmarks/Release/csv_parser_multi_pass_bench.exe --benchmark_format=json data/bench_8col_500k.csv
build/benchmarks/Release/csv_parser_fast_cpp_read_bench.exe --benchmark_format=json data/bench_8col_500k.csv
build/benchmarks/Release/fast_cpp_csv_parser_read_bench.exe --benchmark_format=json data/bench_8col_500k.csv
build/benchmarks/Release/fast_cpp_csv_parser_multi_pass_bench.exe --benchmark_format=json data/bench_8col_500k.csv
build/benchmarks/Release/glaze_csv_multi_pass_bench.exe --benchmark_format=json data/bench_8col_500k.csv
build/benchmarks/Release/dataframe_rapidcsv_roundtrip_bench.exe --benchmark_format=json data/bench_8col_500k.csv
```

Or use the helper script, which generates the default 500K-row dataset, builds
the standalone benchmark tree, and runs any benchmark executables that were
available in the build:

```powershell
benchmarks/scripts/run_benchmarks.ps1
```

For Python reader comparisons against stdlib `csv.reader`, build the Python
binding and run:

```powershell
python python/benchmarks/compare_readers.py path/to/input.csv
```

The Python helper prints one tab-separated line per available variant with file
path, file size, rows, columns, elapsed seconds, MiB/s, and rows/s. The matrix
compares stdlib `csv.reader`, `csvpy.reader`, stdlib `csv.DictReader`, and
`csvpy.DictReader`, with separate `csvpy` string-only and `cast=True` runs.

The script writes JSON results to
`benchmarks/results/<row-count>_rows/<profile>/<benchmark-name>.json` and also
copies the exact input CSV locally to
`benchmarks/results/<row-count>_rows/<profile>/benchmark_input.csv`.

By default it runs `clean`, `quoted`, and `multiline` payloads at 500K rows and
5M rows. The helper skips the `fast-cpp-csv-parser` benchmarks on `multiline`
payloads because that library does not support quoted line breaks.

## csv-parser vs fast-cpp-csv-parser

### What To Compare

There are two very different stories here:

1. **Raw parsing throughput**
   - Count rows
   - Read rows
   - Minimal downstream work

2. **Actual ETL / aggregation work**
   - Materialize rows into a reusable row structure
   - Run repeated passes over the data
   - Pay the cost of turning bytes into something you can work with

`fast-cpp-csv-parser` is excellent at the first category. These benchmarks are
included because that speed is real and worth acknowledging.

The second category is where `csv-parser` pulls ahead in these runs. It is
faster on the materialize and materialize+multi-pass workloads shown below, and
it supports multiline CSV files that `fast-cpp-csv-parser` cannot parse.

One more important point: the materialization and multi-pass ETL comparisons
below are intentionally single-parser-worker runs to stay as apples-to-apples
as possible.
In real use, `fast-cpp-csv-parser` stops at parsing. The caller owns row
materialization, repeated-pass analysis, and any thread-pool or chunk-parallel
orchestration.
`csv-parser` ships those higher-level pieces out of the box, including
`chunk_parallel_apply()`, so the benchmark is conservative with respect to the
actual library surface each user gets.

### Raw Parse Throughput

Median real time, 8-column datasets.

| Dataset | csv-parser count | fast-cpp count | csv-parser read | fast-cpp read |
| --- | ---: | ---: | ---: | ---: |
| 500K clean | 146.5 ms | 81.8 ms | 164.9 ms | 82.8 ms |
| 500K quoted | 197.2 ms | 118.8 ms | 223.5 ms | 136.2 ms |
| 5M clean | 547.6 ms | 881.5 ms | 1,087 ms | 944.2 ms |
| 5M quoted | 665.4 ms | 1,153 ms | 1,140 ms | 1,164 ms |

That is the honest "bytes in, rows out" picture. `fast-cpp-csv-parser` wins
the smaller raw throughput tests on the inputs it supports, while the mmap path
is faster on the current 5M-row count runs.

It is also worth noting that `csv-parser` supports a more dynamic row model by
default. If users choose positional access, which is much closer to the access
pattern `fast-cpp-csv-parser` effectively pushes users toward, the mmap path
changes the picture. In this table, `csv-parser SPSC` means one background
producer/parser worker feeding the foreground consumer through the reader queue;
`spec-N` means speculative parallel parsing with N parser workers.
`fast-cpp-csv-parser SPSC` means its default background block reader plus one
foreground parser.

New runs of `csv_parser_fast_cpp_read_bench` also include
`csv_parser_no_background_thread_read_8col`, which disables the background
`CSVReader` worker at runtime.

The no-background-thread result is useful for constrained environments and
extremely cheap caller loops. If user code does almost nothing per row, the
background producer/consumer layer has little downstream work to hide and may
not improve raw throughput. The SPSC-style mode is still the better default for
general use because it decouples parser progress from caller-side work, and it
helps speculative parsing keep the parser side busy while the foreground
consumer performs conversions, filtering, aggregation, or other row handling.

| Dataset | csv-parser no background | csv-parser SPSC | csv-parser spec-2 | csv-parser spec-4 | csv-parser spec-8 | fast-cpp SPSC read |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 500K clean | 155.4 ms | 154.5 ms | 123.9 ms | 76.6 ms | 77.7 ms | 88.0 ms |
| 500K quoted | 192.9 ms | 196.3 ms | 117.6 ms | 89.3 ms | 83.1 ms | 119.1 ms |
| 5M clean | 1,390 ms | 1,442 ms | 975.8 ms | 702.2 ms | 605.0 ms | 915.1 ms |
| 5M quoted | 1,712 ms | 1,889 ms | 1,331 ms | 977.0 ms | 819.9 ms | 1,320 ms |

That is the core performance story for modern `csv-parser`: positional reads
scale with worker threads and overtake `fast-cpp-csv-parser` by 4 threads on
these clean and quoted runs.

### Materialization Throughput

This table compares the one-pass row materialization benchmarks:

- `csv-parser`: `materialize_csvrow_8col`
- `fast-cpp-csv-parser`: `materialize_struct_8col`

Lower is better.

| Dataset | csv-parser | fast-cpp-csv-parser | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 156.8 ms | 218.8 ms | csv-parser |
| 500K quoted | 211.1 ms | 301.9 ms | csv-parser |
| 500K multiline | 207.4 ms | unsupported | csv-parser |
| 5M clean | 739.6 ms | 2,105 ms | csv-parser |
| 5M quoted | 930.6 ms | 2,489 ms | csv-parser |
| 5M multiline | 827.1 ms | unsupported | csv-parser |

### Materialize + Aggregation Throughput

This table compares the closest apples-to-apples ETL benchmark:

- `csv-parser`: `materialize_and_multi_pass_csvrow_8col`
- `fast-cpp-csv-parser`: `materialize_and_multi_pass_struct_8col`

Lower is better.

| Dataset | csv-parser | fast-cpp-csv-parser | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 215.4 ms | 237.1 ms | csv-parser |
| 500K quoted | 262.9 ms | 280.7 ms | csv-parser |
| 500K multiline | 287.5 ms | unsupported | csv-parser |
| 5M clean | 1,251 ms | 2,252 ms | csv-parser |
| 5M quoted | 1,517 ms | 3,090 ms | csv-parser |
| 5M multiline | 1,372 ms | unsupported | csv-parser |

### Takeaway

- `fast-cpp-csv-parser` is still a strong single-parser-worker raw-read
  baseline on the inputs it supports.
- `csv-parser` wins the positional-read comparison once 4 parser workers are
  available.
- `csv-parser` wins the materialization and ETL-style tables above.
- `csv-parser` handles quoted line breaks; `fast-cpp-csv-parser` does not.

## csv-parser vs Glaze

Glaze was added because its reflected struct API is a natural comparison point
for users who want typed CSV deserialization. Its CSV reader is much smaller and
simpler than csv-parser's parser core, which is a reasonable tradeoff for a
general serialization library. These results measure a mixed workload with
strings, quoted fields, numeric fields, and row-oriented access. A dense
number-heavy CSV may move the relative numbers, but this mixed workload is a
realistic target for csv-parser.

### Raw Mixed Read Throughput

This table comes from the same `csv_parser_fast_cpp_read_bench` bundle used in
the fast-cpp comparison. It reports positional string access for csv-parser,
caller-owned strings for `fast-cpp-csv-parser`, and reflected row structs for
Glaze.

| Dataset | csv-parser no background | csv-parser SPSC | csv-parser spec-8 | Glaze read |
| --- | ---: | ---: | ---: | ---: |
| 500K clean | 155.4 ms | 154.5 ms | 77.7 ms | 243.0 ms |
| 500K quoted | 192.9 ms | 196.3 ms | 83.1 ms | 302.5 ms |
| 5M clean | 1,390 ms | 1,442 ms | 605.0 ms | 2,418 ms |
| 5M quoted | 1,712 ms | 1,889 ms | 819.9 ms | 3,122 ms |

### Materialization Throughput

This table compares one-pass materialization into each library's natural row
representation:

- `csv-parser`: reusable `CSVRow` objects backed by parser-owned row data
- Glaze: reflected row structs through `glz::read<glz::opts_csv>`

| Dataset | csv-parser | Glaze | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 156.8 ms | 209.8 ms | csv-parser |
| 500K quoted | 211.1 ms | 267.1 ms | csv-parser |
| 500K multiline | 207.4 ms | 282.9 ms | csv-parser |
| 5M clean | 739.6 ms | 1,972 ms | csv-parser |
| 5M quoted | 930.6 ms | 2,930 ms | csv-parser |
| 5M multiline | 827.1 ms | 2,492 ms | csv-parser |

### Materialize + Aggregation Throughput

Both libraries use their own numeric conversion/deserialization paths for the
numeric fields in this benchmark.

| Dataset | csv-parser | Glaze | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 215.4 ms | 232.6 ms | csv-parser |
| 500K quoted | 262.9 ms | 283.1 ms | csv-parser |
| 500K multiline | 287.5 ms | 287.7 ms | effectively tied |
| 5M clean | 1,251 ms | 2,084 ms | csv-parser |
| 5M quoted | 1,517 ms | 3,381 ms | csv-parser |
| 5M multiline | 1,372 ms | 2,649 ms | csv-parser |

## csv-parser vs rapidcsv

The benchmark edits two columns before save paths, so the test is not merely
"load a file and dump it back unchanged." Both libraries are asked to do small,
realistic table mutation work.

These rapidcsv comparisons are single-threaded table load/save benchmarks. They
do not include a speculative parallel `CSVReader`, `DataFrameExecutor`,
`column_parallel_apply()`, or `chunk_parallel_apply()`; using those primitives
would likely widen the gap for real ETL workloads.

### 500K Rows

Median real time, lower is better.

| Dataset | csv-parser load | rapidcsv load | csv-parser load+save | rapidcsv load+save |
| --- | ---: | ---: | ---: | ---: |
| clean | 156.7 ms | 244.7 ms | 310.5 ms | 683.0 ms |
| quoted | 211.6 ms | 330.1 ms | 409.8 ms | 890.5 ms |
| multiline | 220.4 ms | 325.2 ms | 412.7 ms | 905.8 ms |

### 5M Rows

Median real time, lower is better.

| Dataset | csv-parser load | rapidcsv load | csv-parser load+save | rapidcsv load+save |
| --- | ---: | ---: | ---: | ---: |
| clean | 666.3 ms | 2,587 ms | 2,070 ms | 7,140 ms |
| quoted | 951.6 ms | 3,450 ms | 2,910 ms | 10,425 ms |
| multiline | 771.1 ms | 3,494 ms | 2,515 ms | 8,987 ms |

### Headline Ratios

Some representative median ratios:

| Workflow | Dataset | Result |
| --- | --- | ---: |
| Load | 500K clean | csv-parser 1.56x faster |
| Load+save | 500K quoted | csv-parser 2.17x faster |
| Load | 5M multiline | csv-parser 4.53x faster |
| Load+save | 5M clean | csv-parser 3.45x faster |
| Load+save | 5M quoted | csv-parser 3.58x faster |

## Notes On Interpretation

- These are Windows/MSVC results. The relative ordering may shift on different
  compilers, CPUs, or storage setups.
- Google Benchmark adds its own harness overhead, so absolute runtimes here are
  not the same as a minimal hand-timed production loop. Real-world throughput is
  often somewhat better, especially for the shortest benchmarks.
- The thread-count comparisons are Google Benchmark tests too. They are useful
  for relative comparisons, but a dedicated application loop will usually give a
  cleaner view of peak throughput.
- Some row-count benchmarks showed noisy count-only means on the 5M datasets, so
  the tables above use **median real time** instead of means.
- The `multiline` profile matters. Many CSV libraries look great until quoted
  line breaks appear.
- "Faster parser" and "faster ETL tool" are not the same claim. This directory
  intentionally measures both.

## Result Files

Raw JSON outputs for the current benchmark runs live under:

```text
benchmarks/results/
```

The repository tracks the benchmark JSON files. The generated CSV inputs are
kept out of git because they are large and reproducible.

Each profile directory may contain:

- `benchmark_input.csv`: the exact generated input used for the run, ignored by
  git
- `csv_parser_read_bench.json`
- `csv_parser_multi_pass_bench.json`
- `csv_parser_fast_cpp_read_bench.json` where applicable; includes the
  side-by-side csv-parser, fast-cpp-csv-parser, and Glaze read comparison when
  those dependencies are available
- `fast_cpp_csv_parser_read_bench.json` where applicable
- `fast_cpp_csv_parser_multi_pass_bench.json` where applicable
- `glaze_csv_read_bench.json` where applicable
- `glaze_csv_multi_pass_bench.json` where applicable
- `dataframe_rapidcsv_roundtrip_bench.json`
