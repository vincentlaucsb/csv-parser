# csvpy Python Bindings

The optional Python package is named `csvpy`; it does not replace or shadow
Python's stdlib `csv` module.

`csvpy.reader()` and `csvpy.DictReader()` provide a stdlib-like facade over the
pybind11 binding. The default behavior matches Python's `csv` module closely:
fields are returned as strings unless you explicitly request scalar casting.

## Building

Build the extension with `BUILD_PYTHON=ON`:

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

## Reader API

Use `csvpy.reader()` for row lists:

```python
import csvpy

with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.reader(handle):
        assert all(isinstance(value, str) for value in row)
```

Use `csvpy.DictReader()` for dictionary rows. The first row is used as headers
unless `fieldnames` is provided:

```python
with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.DictReader(handle):
        print(row["name"])
```

Pass `cast=True` only when you want csv-parser's scalar classification exposed
as Python values. Empty fields become `None`, boolean fields become `bool`,
integral fields become `int`, floating point fields become `float`, timestamp
fields become `datetime.datetime`, and all other fields remain `str`.

```python
rows = list(csvpy.reader(["id,amount,active\n", "1,2.5,true\n"], cast=True))
assert rows == [["id", "amount", "active"], [1, 2.5, True]]
```

The facade supports the common `delimiter`, `quotechar`, `doublequote=True`,
`skipinitialspace`, `strict`, and `fieldnames` options. Unsupported dialect
features intentionally fail fast instead of silently diverging from stdlib
behavior.

The lower-level pybind11 API remains available as `csvpy.Reader`, `csvpy.Format`,
`csvpy.Field`, and related classes.

## Benchmarks

To compare reader throughput locally:

```powershell
python python/benchmarks/compare_readers.py path/to/input.csv
```

The benchmark helper searches `build/` and `out/` for a compatible built
`csvpy` extension and errors clearly if it is missing. Use the same Python
version that built `csvpy`; a `cp310` extension, for example, will not import
under Python 3.14.

The benchmark matrix compares stdlib `csv.reader`, `csvpy.reader` with strings,
`csvpy.reader` with `cast=True`, stdlib `csv.DictReader`, and `csvpy.DictReader`
with both string and casted values. It reports file path, size, rows, columns,
elapsed seconds, MiB/s, and rows/s.
