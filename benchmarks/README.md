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

- `fast-cpp-csv-parser` remains faster for single-thread count/read loops on
  the supported inputs here.
- With positional access and 4+ parser workers, `csv-parser` overtakes
  `fast-cpp-csv-parser` on both clean and quoted read workloads.
- `csv-parser` wins the materialization and materialize+aggregation ETL
  benchmarks shown below, while also supporting multiline CSV that
  `fast-cpp-csv-parser` cannot parse.
- `csv-parser` beats `rapidcsv` for both DataFrame load and edited load+save
  workflows at 500K and 5M rows.

All tables below use **median real time** from the Google Benchmark output.

## Scope

- `csv_parser_read_bench`: read benchmarks for this library, using the filename
  constructor and therefore the native mmap parser where supported.
- `csv_parser_multi_pass_bench`: single-threaded materialization and multi-pass
  ETL-style benchmarks using reusable `CSVRow` objects.
- `csv_parser_fast_cpp_read_bench`: one-binary positional-read comparison
  between this library and `fast-cpp-csv-parser`, with this library measured at
  1, 2, 4, and 8 requested parser threads and `fast-cpp-csv-parser` reported
  once as a single-thread baseline.
- `fast_cpp_csv_parser_read_bench`: read-focused benchmarks for
  `fast-cpp-csv-parser`.
- `fast_cpp_csv_parser_multi_pass_bench`: single-threaded materialization and
  multi-pass ETL-style benchmarks for `fast-cpp-csv-parser`, materializing into
  fixed-width STL rows before running repeated passes.
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
  -DRAPIDCSV_INCLUDE_DIR=C:/src/rapidcsv/src
```

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
below are intentionally single-threaded to stay as apples-to-apples as possible.
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
| 500K clean | 148.1 ms | 82.2 ms | 154.0 ms | 83.0 ms |
| 500K quoted | 199.5 ms | 115.7 ms | 219.7 ms | 117.6 ms |
| 5M clean | 1,507 ms | 889.1 ms | 1,680 ms | 961.1 ms |
| 5M quoted | 1,961 ms | 1,156 ms | 2,221 ms | 1,152 ms |

That is the honest "bytes in, rows out" picture: `fast-cpp-csv-parser` wins
these raw throughput tests on the inputs it supports.

It is also worth noting that `csv-parser` supports a more dynamic row model by
default. If users choose positional access, which is much closer to the access
pattern `fast-cpp-csv-parser` effectively pushes users toward, the speculative
parallel mmap path changes the picture:

| Dataset | csv-parser 1 thread | csv-parser 2 threads | csv-parser 4 threads | csv-parser 8 threads | fast-cpp read |
| --- | ---: | ---: | ---: | ---: | ---: |
| 500K clean | 157.3 ms | 108.6 ms | 75.4 ms | 74.0 ms | 84.3 ms |
| 500K quoted | 214.9 ms | 117.5 ms | 86.8 ms | 83.6 ms | 119.8 ms |
| 5M clean | 1,722 ms | 941.9 ms | 743.7 ms | 699.5 ms | 950.4 ms |
| 5M quoted | 2,059 ms | 1,131 ms | 887.1 ms | 824.2 ms | 1,193 ms |

That is the core performance story for modern `csv-parser`: positional reads
scale with worker threads and overtake `fast-cpp-csv-parser` by 4 threads on
these clean and quoted runs.

### Materialization Throughput

This table compares the one-pass row materialization benchmarks:

- `csv-parser`: `materialize_csvrow_8col`
- `fast-cpp-csv-parser`: `materialize_array_8col`

Lower is better.

| Dataset | csv-parser | fast-cpp-csv-parser | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 154.4 ms | 273.0 ms | csv-parser |
| 500K quoted | 215.6 ms | 310.5 ms | csv-parser |
| 500K multiline | 204.4 ms | unsupported | csv-parser |
| 5M clean | 1,839 ms | 2,511 ms | csv-parser |
| 5M quoted | 2,190 ms | 3,023 ms | csv-parser |
| 5M multiline | 2,182 ms | unsupported | csv-parser |

### Materialize + Aggregation Throughput

This table compares the closest apples-to-apples ETL benchmark:

- `csv-parser`: `materialize_and_multi_pass_csvrow_8col`
- `fast-cpp-csv-parser`: `materialize_and_multi_pass_array_8col`

Lower is better.

| Dataset | csv-parser | fast-cpp-csv-parser | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 196.3 ms | 298.5 ms | csv-parser |
| 500K quoted | 261.0 ms | 335.5 ms | csv-parser |
| 500K multiline | 240.4 ms | unsupported | csv-parser |
| 5M clean | 2,224 ms | 2,681 ms | csv-parser |
| 5M quoted | 2,692 ms | 3,230 ms | csv-parser |
| 5M multiline | 2,689 ms | unsupported | csv-parser |

### Takeaway

- `fast-cpp-csv-parser` is still faster for single-thread raw count/read loops.
- `csv-parser` wins the positional-read comparison once 4 parser workers are
  available.
- `csv-parser` wins the materialization and ETL-style tables above.
- `csv-parser` handles quoted line breaks; `fast-cpp-csv-parser` does not.

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
| clean | 153.6 ms | 249.8 ms | 314.2 ms | 722.6 ms |
| quoted | 215.6 ms | 320.2 ms | 404.9 ms | 909.7 ms |
| multiline | 215.6 ms | 319.8 ms | 393.0 ms | 908.4 ms |

### 5M Rows

Median real time, lower is better.

| Dataset | csv-parser load | rapidcsv load | csv-parser load+save | rapidcsv load+save |
| --- | ---: | ---: | ---: | ---: |
| clean | 1,837 ms | 2,629 ms | 3,356 ms | 7,503 ms |
| quoted | 2,270 ms | 3,468 ms | 4,026 ms | 9,596 ms |
| multiline | 2,225 ms | 3,402 ms | 3,941 ms | 9,498 ms |

### Headline Ratios

Some representative median ratios:

| Workflow | Dataset | Result |
| --- | --- | ---: |
| Load | 500K clean | csv-parser 1.63x faster |
| Load+save | 500K quoted | csv-parser 2.25x faster |
| Load | 5M multiline | csv-parser 1.53x faster |
| Load+save | 5M clean | csv-parser 2.24x faster |
| Load+save | 5M quoted | csv-parser 2.38x faster |

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
- `csv_parser_fast_cpp_read_bench.json` where applicable
- `fast_cpp_csv_parser_read_bench.json` where applicable
- `fast_cpp_csv_parser_multi_pass_bench.json` where applicable
- `dataframe_rapidcsv_roundtrip_bench.json`
