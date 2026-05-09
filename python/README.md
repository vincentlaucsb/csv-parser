# csvpy Python Bindings

The optional Python package is named `csvpy`; it does not replace or shadow
Python's stdlib `csv` module.

`csvpy.reader()` yields lazy, list-like row objects over the nanobind extension.
Fields are returned as strings unless you explicitly request scalar casting, and
individual fields are materialized only when accessed.

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

The `csvpy` build fetches nanobind with CMake `FetchContent` only when the
Python binding is requested. A normal C++ library/test build does not require
the nanobind checkout.

## Reader API

Use `csvpy.reader()` for lazy row objects:

```python
import csvpy

with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.reader(handle):
        assert isinstance(row[0], str)
        values = row.as_list()  # Materialize explicitly when needed.
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
assert rows[0].as_list() == ["id", "amount", "active"]
assert rows[1].as_list() == [1, 2.5, True]
```

The facade supports the common `delimiter`, `quotechar`, `doublequote=True`,
`skipinitialspace`, `strict`, and `fieldnames` options. Unsupported dialect
features intentionally fail fast instead of silently diverging from stdlib
behavior.

The lower-level extension API remains available as `csvpy.Reader`, `csvpy.Format`,
`csvpy.Field`, and related classes.

## Benchmarks

To compare reader throughput locally:

```powershell
python python/benchmarks/compare_readers.py path/to/input.csv
```

To compare the streaming EDA script against pandas using the pyarrow CSV engine:

```powershell
python python/benchmarks/compare_eda.py path/to/input.csv
```

To compare a streaming `csvpy` filter against pyarrow on the Used Cars-style
`region == "el paso"` workload:

```powershell
python python/benchmarks/compare_filter.py path/to/vehicles.csv
```

This runs each workload in a fresh Python process and reports wall time,
throughput, and peak resident/working-set memory.

The benchmark helper searches `build/` and `out/` for a compatible built
`csvpy` extension and errors clearly if it is missing. Use the same Python
version that built `csvpy`; a `cp310` extension, for example, will not import
under Python 3.14.

The benchmark matrix compares stdlib `csv.reader`, lazy `csvpy.reader` rows with
strings, lazy `csvpy.reader` rows with `cast=True`, stdlib `csv.DictReader`, and
`csvpy.DictReader` with both string and casted values. It reports file path,
size, rows, columns, elapsed seconds, MiB/s, and rows/s.

## Exploratory Summary

For a small dependency-free EDA pass over a CSV:

```powershell
python python/examples/eda_summary.py path/to/input.csv
```

The script streams lazy `csvpy` rows once and reports per-column mean, sample
standard deviation, null count, and top observed values. Histograms use a
bounded approximate heavy-hitter sketch by default to keep memory usage stable
on high-cardinality columns. Use `--top-n N` to change the report size,
`--top-capacity N` to change the per-column sketch size, `--exact-values` for
exact value counts, or `--no-header` for headerless files.
