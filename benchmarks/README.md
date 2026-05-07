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
- WDC `WD40EZAZ-00SF3B0` storage

The short version:

- `csv-parser` is not the fastest library for bare "count rows as quickly as
  possible" benchmarks. `fast-cpp-csv-parser` is very strong there.
- When users choose positional access, `csv-parser` now beats
  `fast-cpp-csv-parser` on clean workloads once the speculative parallel mmap
  path has enough worker threads. That is the closest raw-read comparison
  because both libraries are using positional fields.
- Heavily quoted workloads are still the main exception:
  `fast-cpp-csv-parser` remains faster there on the quoted datasets it can
  parse.
- For ETL-style work, `csv-parser` remains competitive and often faster on
  clean data, handles multiline CSV that `fast-cpp-csv-parser` cannot parse at
  all, and provides chunked/parallel processing primitives instead of making
  every user build that machinery themselves.
- Against `rapidcsv`, the 500K-row run still favors `csv-parser`. The 5M-row
  run exposed a `DataFrame` append-capacity regression and should be regenerated
  before drawing a release-note conclusion.

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

For Python reader comparisons against stdlib `csv.reader` and pandas with the
Apache Arrow CSV engine/backend when installed, build the Python binding and run:

```powershell
python python/benchmarks/compare_readers.py path/to/input.csv
```

The Python helper prints one tab-separated line per available variant with file
path, file size, rows, columns, elapsed seconds, MiB/s, and rows/s. Missing
optional pandas or pyarrow dependencies are reported as explicit skips.

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

The second category is where the libraries trade wins. `csv-parser` is faster
on clean materialize+multi-pass workloads, while `fast-cpp-csv-parser` remains
faster on the heavily quoted workloads it supports. `csv-parser` also supports
multiline CSV files that `fast-cpp-csv-parser` cannot parse.

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
| 500K clean | 176.2 ms | 84.9 ms | 178.7 ms | 91.6 ms |
| 500K quoted | 178.9 ms | 133.1 ms | 360.0 ms | 133.6 ms |
| 5M clean | 1,502 ms | 853.6 ms | 1,740 ms | 861.1 ms |
| 5M quoted | 1,640 ms | 1,193 ms | 3,273 ms | 1,195 ms |

That is the honest "bytes in, rows out" picture: `fast-cpp-csv-parser` wins
these raw throughput tests on the inputs it supports.

It is also worth noting that `csv-parser` supports a more dynamic row model by
default. If users choose positional access, which is much closer to the access
pattern `fast-cpp-csv-parser` effectively pushes users toward, the speculative
parallel mmap path changes the picture:

| Dataset | csv-parser 1 thread | csv-parser 2 threads | csv-parser 4 threads | csv-parser 8 threads | fast-cpp read |
| --- | ---: | ---: | ---: | ---: | ---: |
| 500K clean | 186.6 ms | 108.5 ms | 85.1 ms | 77.7 ms | 87.4 ms |
| 500K quoted | 332.5 ms | 288.7 ms | 250.1 ms | 245.2 ms | 131.5 ms |
| 5M clean | 1,791 ms | 869.6 ms | 784.6 ms | 732.6 ms | 862.0 ms |
| 5M quoted | 3,697 ms | 2,574 ms | 2,334 ms | 2,391 ms | 1,353 ms |

That is the core performance story for modern `csv-parser`: clean positional
reads scale with worker threads and overtake `fast-cpp-csv-parser` by 4 threads
on these runs. Heavy quoting is still expensive for this parser, and
`fast-cpp-csv-parser` remains faster there when the input is within its
supported CSV surface.

### Materialization Throughput

This table compares the one-pass row materialization benchmarks:

- `csv-parser`: `materialize_csvrow_8col`
- `fast-cpp-csv-parser`: `materialize_array_8col`

Lower is better.

| Dataset | csv-parser | fast-cpp-csv-parser | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 179.9 ms | 288.3 ms | csv-parser |
| 500K quoted | 198.6 ms | 322.6 ms | csv-parser |
| 500K multiline | 198.3 ms | unsupported | csv-parser |
| 5M clean | 1,856 ms | 2,523 ms | csv-parser |
| 5M quoted | 2,037 ms | 2,969 ms | csv-parser |
| 5M multiline | 2,398 ms | unsupported | csv-parser |

### Materialize + Aggregation Throughput

This table compares the closest apples-to-apples ETL benchmark:

- `csv-parser`: `materialize_and_multi_pass_csvrow_8col`
- `fast-cpp-csv-parser`: `materialize_and_multi_pass_array_8col`

Lower is better.

| Dataset | csv-parser | fast-cpp-csv-parser | Winner |
| --- | ---: | ---: | --- |
| 500K clean | 231.2 ms | 305.4 ms | csv-parser |
| 500K quoted | 458.4 ms | 343.6 ms | fast-cpp-csv-parser |
| 500K multiline | 337.2 ms | unsupported | csv-parser |
| 5M clean | 2,603 ms | 2,769 ms | csv-parser |
| 5M quoted | 4,779 ms | 3,169 ms | fast-cpp-csv-parser |
| 5M multiline | 3,896 ms | unsupported | csv-parser |

### Takeaway

- If all you care about is single-thread raw parsing speed on supported inputs,
  `fast-cpp-csv-parser` is faster.
- If you can use multiple parser threads and positional access, `csv-parser`
  overtakes it on the clean datasets here.
- If you care about **materializing rows into a workable structure**, the
  numbers here strongly favor `csv-parser`.
- If you care about **end-to-end ETL work on clean data**, `csv-parser` is
  faster in these benchmarks as well.
- If your workload is **heavily quoted but not multiline**, that remains the
  clearest `fast-cpp-csv-parser` win in this benchmark set.
- If you care about **parallel ETL ergonomics**, `csv-parser` already provides
  chunked processing and parallel apply primitives, while
  `fast-cpp-csv-parser` leaves that infrastructure to the caller.
- If your CSVs contain **quoted line breaks**, `csv-parser` still runs and
  `fast-cpp-csv-parser` does not.

That is the actual tradeoff surface, and it is more useful than pretending one
library dominates every category.

## csv-parser vs rapidcsv

This comparison needs one more local rerun before release. The latest checked-in
JSON exposed a `DataFrame` scaling regression: append batches were reserving
exactly one batch ahead, which forced repeated reallocations on the 5M-row
inputs. That has been fixed, so the 5M rapidcsv numbers below should be treated
as stale diagnostic data until this benchmark is regenerated.

The benchmark edits two columns before save paths, so the test is not merely
"load a file and dump it back unchanged." Both libraries are asked to do small,
realistic table mutation work.

### 500K Rows

Median real time, lower is better.

| Dataset | csv-parser load | rapidcsv load | csv-parser load+save | rapidcsv load+save |
| --- | ---: | ---: | ---: | ---: |
| clean | 186.6 ms | 252.2 ms | 326.8 ms | 686.7 ms |
| quoted | 204.7 ms | 329.0 ms | 565.5 ms | 901.9 ms |
| multiline | 211.6 ms | 322.5 ms | 503.7 ms | 877.3 ms |

### 5M Rows

Median real time, lower is better.

| Dataset | csv-parser load | rapidcsv load | csv-parser load+save | rapidcsv load+save |
| --- | ---: | ---: | ---: | ---: |
| clean | 6,469 ms | 2,830 ms | 6,921 ms | 7,694 ms |
| quoted | 5,506 ms | 3,716 ms | 9,510 ms | 9,642 ms |
| multiline | 5,520 ms | 3,660 ms | 8,938 ms | 9,410 ms |

### Headline Ratios

Some representative median ratios from the stale run:

| Workflow | Dataset | Result |
| --- | --- | ---: |
| Load | 500K quoted | csv-parser 1.61x faster |
| Load+save | 500K clean | csv-parser 2.10x faster |
| Load | 5M clean | rapidcsv 2.29x faster |
| Load+save | 5M clean | csv-parser 1.11x faster |
| Load+save | 5M multiline | csv-parser 1.05x faster |

The temporary conclusion:

- `csv-parser` wins the 500K-row load and edited round-trip cases.
- The 5M rows exposed a `DataFrame` construction regression and should be rerun
  before drawing a release-note conclusion.

That makes this section a useful reminder that benchmark anomalies are often
more valuable as smoke tests than as marketing copy. Do not ship the 5M rapidcsv
claim until the fixed build has produced fresh JSON.

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
