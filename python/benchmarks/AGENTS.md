# Python Benchmark Agent Context

This directory contains Python-side benchmark helpers, including
`compare_readers.py`.

## Building `csvpy`

`compare_readers.py` expects the pybind11 `csvpy` extension to be built for the
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

The `csvpy` build fetches pybind11 with CMake `FetchContent` only when the
Python binding is requested. A normal C++ library/test build does not require
the pybind11 checkout.

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
