# Python Benchmark Agent Context

This directory contains Python-side benchmark helpers. Shared process,
extension-discovery, and result-formatting plumbing lives in `_support.py`; keep
individual benchmark scripts focused on the workload they measure.

Subprocess benchmarks should resolve the built `fastpycsv` extension in the
parent process via `_support.benchmark_env()` and pass
`FASTPYCSV_EXTENSION_PATH` to workers. Do not make each measured worker
recursively scan `build/`; that discovery cost swamps small runs and adds noise.

## Building `fastpycsv`

`compare_readers.py` expects the nanobind `fastpycsv` extension to be built for the
same Python interpreter used to run the script. From the repository root:

```powershell
cmake -S . -B build/fastpycsv -DBUILD_PYTHON=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/fastpycsv --target fastpycsv --config Release
```

For an existing top-level build configured without `BUILD_PYTHON=ON`, build the
`fastpycsv` target directly. It bootstraps a separate `build/fastpycsv` tree with
`BUILD_PYTHON=ON`, so the main build does not need to be reconfigured:

```powershell
cmake --build build/x64-Release --target fastpycsv --config Release
```

To force a specific interpreter, configure the main build with:

```powershell
cmake -S . -B build/x64-Release -DFASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE=C:/Python314/python.exe
```

The `fastpycsv` build fetches nanobind with CMake `FetchContent` only when the
Python binding is requested. A normal C++ library/test build does not require
the nanobind checkout.

If the target is missing, re-run CMake generation for the main build:

```powershell
cmake -S . -B build/x64-Release
```

## Running Reader Comparisons

Run from any directory; the helper searches `build/` and `out/` for a compatible
built `fastpycsv` extension and errors clearly if it is missing:

```powershell
python python/benchmarks/compare_readers.py path/to/input.csv
```

Most Python comparison scripts accept `--only` to run one library family at a
time, for example `--only fastpycsv`, `--only pyarrow`, or `--only polars`.
Repeat `--only` to include a small subset of libraries in one run.

For EDA wall-time and memory comparisons against pandas with the pyarrow CSV
engine:

```powershell
python python/benchmarks/compare_eda.py path/to/input.csv
```

`compare_eda.py` launches each workload in a fresh Python process and samples
peak resident/working-set memory. The fastpycsv side uses the bounded approximate
top-value sketch by default; pass `--fastpycsv-exact-values` only when exact
histograms are required. This script exists to sanity-check bounded-memory
streaming ergonomics, not to claim that GIL-bound Python row aggregation should
beat pandas, pyarrow, or Polars on DataFrame-style summaries.

For best-path CSV filtering across fastpycsv, pyarrow, and Polars, defaulting
to the Craigslist Used Cars `region == "el paso"` workload:

```powershell
python python/benchmarks/compare_filter.py path/to/vehicles.csv
```

Use repeated `--filter` arguments for multi-column filters. Quote filters that
contain `<` or `>` in PowerShell:

```powershell
python python/benchmarks/compare_filter.py path/to/vehicles.csv --filter region="el paso" --filter 'price<10000'
```

The filter head-to-head scripts should present one best available path per
library. fastpycsv should use native predicates with
`FASTPYCSV_PREDICATE_PARALLEL=1`, pyarrow should use CSV projection plus compute
kernels, and Polars should use lazy/eager filter expressions as appropriate for
plain CSV vs ZIP member inputs.

For ZIP-contained CSV filtering across the same library set, defaulting to Dil
Wong's flight prices `destinationAirport == "PHX"` workload:

```powershell
python python/benchmarks/compare_zip_filter.py path/to/flight_prices.zip
```

For selected-column CSV-to-NumPy materialization against pyarrow:

```powershell
python python/benchmarks/compare_numpy_materialization.py path/to/vehicles.csv
```

For Python object materialization against pyarrow and Polars:

```powershell
python python/benchmarks/compare_python_materialization.py path/to/vehicles.csv
```

This compares full CSV and first+last-column subset materialization for
row-oriented `list[dict]` outputs and column-oriented `dict[str, list]` outputs.

The reader benchmark compares stdlib `csv.reader`, stdlib `csv.DictReader`,
lazy `fastpycsv.reader` rows with strings, and lazy `fastpycsv.reader` rows with
`cast=True`. Keep DataFrame/Table libraries and NumPy materialization out of
this script; those workloads belong in the dedicated materialization
benchmarks.

Use the same Python version that built `fastpycsv`. A `cp310` extension, for
example, will not import under Python 3.14.

## C++ Benchmark Suite

The standalone C++ benchmark suite lives under `benchmarks/`. Build it from the
repository root:

```powershell
cmake -S benchmarks -B build/benchmarks -DCMAKE_BUILD_TYPE=Release
cmake --build build/benchmarks --config Release
```

See `benchmarks/README.md` for dataset generation, optional comparison
libraries, and result-output conventions.
