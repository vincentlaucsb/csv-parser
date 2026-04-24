# CSV Parser Comparison Benchmarks

This directory is intentionally standalone. It is not referenced by the top-level
build, so comparison benchmarking remains opt-in and cannot slow normal
developer or CI builds.

## Scope

- `csv_parser_read_bench`: read benchmarks for this library, using the filename
  constructor and therefore the native mmap parser where supported.
- `fast_cpp_csv_parser_read_bench`: read-focused benchmarks for
  `fast-cpp-csv-parser`.
- `dataframe_rapidcsv_roundtrip_bench`: table/round-trip-oriented benchmarks
  comparing this library's `DataFrame` workflow with `rapidcsv`, including
  load-only, save-only, and full round-trip cases.

The rapidcsv benchmark uses `rapidcsv::Document::Save()` for the rapidcsv write
side. The csv-parser DataFrame benchmark writes through this library's
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
  line breaks. This is the high-throughput baseline.
- `quoted`: realistic quoted commas and doubled quotes in the `note` column,
  but no embedded line breaks. This keeps the file RFC-style without requiring
  multiline-field support.
- `multiline`: quoted commas, doubled quotes, and embedded line breaks in the
  `note` column to exercise full multiline CSV handling.

Two row shapes are supported:

- `standard`: the original compact schema values.
- `wide`: longer string values across the existing 8 columns, intended for
  larger-file benchmarks where row width should matter more.

Use enough rows to cross the library's 10MB chunk boundary when measuring
csv-parser behavior. At least 500K rows is the project baseline for large-file
coverage.

Example run:

```powershell
build/benchmarks/Release/csv_parser_read_bench.exe --benchmark_format=json data/bench_8col_500k.csv
build/benchmarks/Release/fast_cpp_csv_parser_read_bench.exe --benchmark_format=json data/bench_8col_500k.csv
build/benchmarks/Release/dataframe_rapidcsv_roundtrip_bench.exe --benchmark_format=json data/bench_8col_500k.csv
```

Or use the helper script, which generates the default 500K-row dataset, builds
the standalone benchmark tree, and runs any benchmark executables that were
available in the build:

```powershell
benchmarks/scripts/run_benchmarks.ps1
```

The script keeps terminal output concise and writes full Google Benchmark JSON
results to `benchmarks/results/<row-count>_rows/<profile>/<benchmark-name>.json`
by default. It writes through a temporary file and validates JSON before
replacing the final result, so a failed benchmark cannot leave a truncated
`.json` as the latest result. It also copies the generated CSV used for each
run to `benchmarks/results/<row-count>_rows/<profile>/benchmark_input.csv`,
replacing the previous copy on reruns. By default it runs 5 repetitions and
emits aggregate-only console/JSON output to reduce noise from single-iteration
outliers. Override the output directory with `-ResultsDir`.

By default the script runs `clean`, `quoted`, and `multiline` payloads at
500K rows and 5M rows. Generated datasets are kept under
`build/benchmarks/data` and are not regenerated when they already exist. Use
`-ForceDatasets` to recreate them, pass `-Rows 10` for a quick smoke run, or
pass `-Profiles clean` when you want to isolate the simple payload. The helper
skips `fast_cpp_csv_parser_read_bench` on the `multiline` payload because
fast-cpp-csv-parser does not support quoted line breaks. For compatibility,
`-Profiles realistic` is still accepted as an alias for `multiline`.

For larger experiments, the generator can stop by approximate file size instead
of row count. Use `-TargetSizeGb 2 -RowShape wide` to build a wider dataset
that targets roughly 2 GiB while preserving the same 8-column schema.
