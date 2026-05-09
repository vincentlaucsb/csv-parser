# Python Benchmark Agent Context

This directory contains Python-side benchmark helpers, including
`compare_readers.py`, `compare_eda.py`, and `compare_filter.py`.

## Building `csvpy`

`compare_readers.py` expects the nanobind `csvpy` extension to be built for the
same Python interpreter used to run the script. From the repository root:

```powershell
cmake -S . -B build/csvpy -DBUILD_PYTHON=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/csvpy --target csvpy --config Release
```

For an existing top-level build configured without `BUILD_PYTHON=ON`, build the
`csvpy` target directly. It bootstraps a separate `build/csvpy` tree with
`BUILD_PYTHON=ON`, so the main build does not need to be reconfigured:

```powershell
cmake --build build/x64-Release --target csvpy --config Release
```

To force a specific interpreter, configure the main build with:

```powershell
cmake -S . -B build/x64-Release -DCSVPY_BOOTSTRAP_PYTHON_EXECUTABLE=C:/Python314/python.exe
```

The `csvpy` build fetches nanobind with CMake `FetchContent` only when the
Python binding is requested. A normal C++ library/test build does not require
the nanobind checkout.

If the target is missing, re-run CMake generation for the main build:

```powershell
cmake -S . -B build/x64-Release
```

## Running Reader Comparisons

Run from any directory; the helper searches `build/` and `out/` for a compatible
built `csvpy` extension and errors clearly if it is missing:

```powershell
python python/benchmarks/compare_readers.py path/to/input.csv
```

For EDA wall-time and memory comparisons against pandas with the pyarrow CSV
engine:

```powershell
python python/benchmarks/compare_eda.py path/to/input.csv
```

`compare_eda.py` launches each workload in a fresh Python process and samples
peak resident/working-set memory. The csvpy side uses the bounded approximate
top-value sketch by default; pass `--csvpy-exact-values` only when exact
histograms are required.

For a streaming filter/subset comparison against pyarrow, defaulting to the
Craigslist Used Cars `region == "el paso"` workload:

```powershell
python python/benchmarks/compare_filter.py path/to/vehicles.csv
```

The benchmark matrix compares stdlib `csv.reader`, lazy `csvpy.reader` rows with
strings, lazy `csvpy.reader` rows with `cast=True`, stdlib `csv.DictReader`, and
`csvpy.DictReader` with both string and casted values. Keep DataFrame/Table
libraries out of this script unless the benchmark explicitly normalizes outputs
first.

Use the same Python version that built `csvpy`. A `cp310` extension, for
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
