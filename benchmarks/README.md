# CSV Parser Comparison Benchmarks

This directory is intentionally standalone. It is not referenced by the
top-level build, so comparison benchmarking remains opt-in and cannot slow
normal developer or CI builds.

## Executive Summary

These benchmarks were run on:

- Windows 11 Home 25H2 (`OS build 26200.8246`)
- Visual Studio 2022 / MSVC 17.14
- Windows SDK `10.0.26100.0`
- C++20
- AVX2 enabled
- `CSV_ENABLE_THREADS=ON`
- Google Benchmark 1.8.5
- 12th Gen Intel(R) Core(TM) i5-12400
- 12 logical CPUs at ~2.5 GHz
- WDC `WD40EZAZ-00SF3B0` storage

The short version:

- `csv-parser` is not the fastest library for bare "count rows as quickly as
  possible" benchmarks. `fast-cpp-csv-parser` is very strong there.
- For actual ETL-style work, `csv-parser` is already faster on the clean-data
  workloads here at the **materialization** stage, stays faster for the full
  materialize+multi-pass ETL path, and remains usable on multiline CSV that
  `fast-cpp-csv-parser` cannot parse at all.
- Against `rapidcsv`, `csv-parser` wins every measured load, save, and
  load+save benchmark, often by a very wide margin.

All tables below use **median real time** from the Google Benchmark output.

## Scope

- `csv_parser_read_bench`: read benchmarks for this library, using the filename
  constructor and therefore the native mmap parser where supported.
- `csv_parser_multi_pass_bench`: single-threaded materialization and multi-pass
  ETL-style benchmarks using reusable `CSVRow` objects.
- `fast_cpp_csv_parser_read_bench`: read-focused benchmarks for
  `fast-cpp-csv-parser`.
- `fast_cpp_csv_parser_multi_pass_bench`: single-threaded materialization and
  multi-pass ETL-style benchmarks for `fast-cpp-csv-parser`, materializing into
  fixed-width STL rows before running repeated passes.
- `dataframe_rapidcsv_roundtrip_bench`: table/round-trip-oriented benchmarks
  comparing this library's `DataFrame` workflow with `rapidcsv`, including
  load-only, save-only, and full round-trip cases.

The rapidcsv benchmark uses `rapidcsv::Document::Save()` for the rapidcsv write
side. The csv-parser benchmark writes through this library's `CSVWriter`, so
benchmark names call that out explicitly.

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

The script writes JSON results to
`benchmarks/results/<row-count>_rows/<profile>/<benchmark-name>.json` and also
copies the exact input CSV to
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

The second category is where `csv-parser` pulls ahead on the clean-data
workloads below. That advantage is visible not only in the full
materialize+multi-pass path, but already in the one-pass materialization stage.
It also supports multiline CSV files that `fast-cpp-csv-parser` cannot parse.

One more important point: the ETL comparisons below are intentionally
single-threaded to stay as apples-to-apples as possible. In real use,
`fast-cpp-csv-parser` stops at parsing. The caller owns row materialization,
repeated-pass analysis, and any thread-pool or chunk-parallel orchestration.
`csv-parser` ships those higher-level pieces out of the box, including
`chunk_parallel_apply()`, so the benchmark is conservative with respect to the
actual library surface each user gets.

### Raw Parse Throughput

Median real time, 8-column datasets.

| Dataset | csv-parser count | fast-cpp count | csv-parser read | fast-cpp read |
| --- | ---: | ---: | ---: | ---: |
| 500K clean | 134 ms | 77.6 ms | 163 ms | 83.9 ms |
| 500K quoted | 151 ms | 104 ms | 314 ms | 107 ms |
| 5M clean | 1175 ms | 830 ms | 1622 ms | 799 ms |
| 5M quoted | 1336 ms | 1034 ms | 2958 ms | 1037 ms |

That is the honest "bytes in, rows out" picture: `fast-cpp-csv-parser` wins
these raw throughput tests on the inputs it supports.

It is also worth noting that `csv-parser` supports a more dynamic row model by
default. If users choose positional access, which is much closer to the access
pattern `fast-cpp-csv-parser` effectively pushes users toward, the gap narrows:

| Dataset | csv-parser positional read | fast-cpp read |
| --- | ---: | ---: |
| 500K clean | 139 ms | 83.9 ms |
| 500K quoted | 258 ms | 107 ms |
| 5M clean | 1288 ms | 799 ms |
| 5M quoted | 2556 ms | 1037 ms |

That is still not a win on raw throughput, but it is a more apples-to-apples
comparison than treating dynamic column lookup and fixed positional access as
the same workload.

### Materialization Throughput

This table compares the one-pass row materialization benchmarks:

- `csv-parser`: `materialize_csvrow_8col`
- `fast-cpp-csv-parser`: `materialize_array_8col`

Lower is better.

| Dataset | csv-parser | fast-cpp-csv-parser | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 151 ms | 280 ms | csv-parser |
| 500K quoted | 175 ms | 300 ms | csv-parser |
| 500K multiline | 171 ms | unsupported | csv-parser |
| 5M clean | 1648 ms | 2733 ms | csv-parser |
| 5M quoted | 1715 ms | 2707 ms | csv-parser |
| 5M multiline | 1859 ms | unsupported | csv-parser |

### Materialize + Aggregation Throughput

This table compares the closest apples-to-apples ETL benchmark:

- `csv-parser`: `materialize_and_multi_pass_csvrow_8col`
- `fast-cpp-csv-parser`: `materialize_and_multi_pass_array_8col`

Lower is better.

| Dataset | csv-parser | fast-cpp-csv-parser | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 173 ms | 290 ms | csv-parser |
| 500K quoted | 384 ms | 326 ms | fast-cpp-csv-parser |
| 500K multiline | 329 ms | unsupported | csv-parser |
| 5M clean | 2108 ms | 2625 ms | csv-parser |
| 5M quoted | 4278 ms | 3105 ms | fast-cpp-csv-parser |
| 5M multiline | 3948 ms | unsupported | csv-parser |

### Takeaway

- If all you care about is raw parsing speed on simple supported inputs,
  `fast-cpp-csv-parser` is faster.
- If you care about **materializing rows into a workable structure**, the
  numbers here strongly favor `csv-parser`.
- If you care about **end-to-end ETL work on clean data**, `csv-parser` is
  faster in these benchmarks as well.
- If you care about **parallel ETL ergonomics**, `csv-parser` already provides
  chunked processing and parallel apply primitives, while
  `fast-cpp-csv-parser` leaves that infrastructure to the caller.
- If your CSVs contain **quoted line breaks**, `csv-parser` still runs and
  `fast-cpp-csv-parser` does not.

That is the actual tradeoff surface, and it is more useful than pretending one
library dominates every category.

## csv-parser vs rapidcsv

This is the simpler comparison.

The benchmark edits two columns before save paths, so the test is not merely
"load a file and dump it back unchanged." Both libraries are asked to do small,
realistic table mutation work.

### 500K Rows

Median real time, lower is better.

| Dataset | csv-parser load | rapidcsv load | csv-parser save | rapidcsv save | csv-parser load+save | rapidcsv load+save |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| clean | 156 ms | 276 ms | 107 ms | 456 ms | 233 ms | 693 ms |
| quoted | 173 ms | 322 ms | 210 ms | 559 ms | 466 ms | 866 ms |
| multiline | 170 ms | 332 ms | 183 ms | 569 ms | 419 ms | 916 ms |

### 5M Rows

Median real time, lower is better.

| Dataset | csv-parser load | rapidcsv load | csv-parser save | rapidcsv save | csv-parser load+save | rapidcsv load+save |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| clean | 1549 ms | 2684 ms | 1059 ms | 4546 ms | 2570 ms | 7067 ms |
| quoted | 1680 ms | 3609 ms | 2806 ms | 5634 ms | 5231 ms | 9979 ms |
| multiline | 1839 ms | 3610 ms | 2544 ms | 5741 ms | 4734 ms | 9505 ms |

### Headline Ratios

Some representative median speedups:

| Workflow | Dataset | Speedup |
| --- | --- | ---: |
| Load | 5M quoted | 2.15x faster |
| Save | 5M clean | 4.29x faster |
| Load+save | 5M clean | 2.75x faster |
| Load+save | 5M multiline | 2.01x faster |

The broad conclusion is straightforward:

- `csv-parser` loads faster
- `csv-parser` saves faster
- `csv-parser` round-trips faster
- `csv-parser` stays ahead on clean, quoted, and multiline data

This is not a narrow win on one cherry-picked case. It is a clean sweep across
every measured `rapidcsv` benchmark here.

## Notes On Interpretation

- These are Windows/MSVC results. The relative ordering may shift on different
  compilers, CPUs, or storage setups.
- Google Benchmark adds its own harness overhead, so absolute runtimes here are
  not the same as a minimal hand-timed production loop. Real-world throughput is
  often somewhat better, especially for the shortest benchmarks.
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

Each profile directory contains:

- `benchmark_input.csv`: the exact generated input used for the run
- `csv_parser_read_bench.json`
- `csv_parser_multi_pass_bench.json`
- `fast_cpp_csv_parser_read_bench.json` where applicable
- `fast_cpp_csv_parser_multi_pass_bench.json` where applicable
- `dataframe_rapidcsv_roundtrip_bench.json`
