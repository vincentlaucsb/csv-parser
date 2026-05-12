# fastpycsv

<p align="center">
  <img src="assets/fastpycsv-logo.png" alt="fastpycsv logo" width="240">
</p>

`fastpycsv` is a fast Python CSV toolkit backed by Vince's CSV Parser. It is built
for ETL-style workflows where users need to scan large CSV files, filter rows,
extract selected columns, materialize NumPy arrays, or write cleaned CSV output
without routing every job through pandas or pyarrow.

The package is named `fastpycsv`; it does not replace or shadow Python's stdlib
`csv` module.

Long-form Python documentation lives in `python/docs/` and is written in
Sphinx/MyST-flavored Markdown so it can be published with Read the Docs later.

Main API:

- `fastpycsv.reader()` yields lazy, list-like row objects over the native parser.
- `fastpycsv.read_numpy()` exports selected columns as eager NumPy arrays.
- `fastpycsv.read_numpy_batches()` streams selected columns as bounded-memory NumPy
  batches.
- `fastpycsv.write_csv()` streams lazy rows or Python iterables through the native
  CSV writer.

## Building

Install the local package directly from the repository root:

```powershell
python -m pip install -e E:\GitHub\csv-parser
```

For a regular non-editable local install:

```powershell
python -m pip install E:\GitHub\csv-parser
```

The package build uses CMake through `scikit-build-core` and builds the native
`fastpycsv` extension for the Python interpreter running `pip`.

Build the extension with `BUILD_PYTHON=ON`:

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

## Reader API

Use `fastpycsv.reader()` for lazy row objects. By default, it consumes the first row
as column names:

```python
import fastpycsv

with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in fastpycsv.reader(handle):
        assert isinstance(row["name"], str)
        values = row.as_list()  # Materialize explicitly when needed.
```

Pass `consume_header=False` when the first input row should be emitted as data:

```python
with open("headerless.csv", newline="", encoding="utf-8") as handle:
    for row in fastpycsv.reader(handle, consume_header=False):
        print(row[0])
```

Call `row.as_dict()` only when you explicitly want to materialize a row as a
plain Python dictionary.

When plain Python row objects are the desired output, use the materialized row
iterators instead of converting each lazy row manually:

```python
predicate = fastpycsv.all_of(
    fastpycsv.equal("region", "el paso", case_sensitive=False),
    fastpycsv.less("price", "15000"),
)
reader = fastpycsv.reader("data.csv").filter(predicate)

for row in reader.lists(["id", "price"]):
    ...

for batch in fastpycsv.reader("data.csv").dicts(["id", "price"]).chunks(50_000):
    ...

rows = fastpycsv.reader("data.csv").tuples(["id", "price"]).all()
```

The `.lists()`, `.tuples()`, and `.dicts()` iterators stream one row at a time.
Their `.chunks(size)` methods keep memory bounded while amortizing native
conversion overhead, and `.all()` is the eager bulk-export path. Native
`reader.filter(predicate)` uses the same `fastpycsv.equal()`, `less()`, `greater()`,
and `all_of()` predicate objects as the NumPy APIs, and validates predicate
column names before rows are consumed.

Most users can ignore `reader(batch_size=...)`. It does not make ordinary
iteration return batches:

```python
for row in fastpycsv.reader("data.csv"):
    ...  # one row at a time
```

Use `.chunks(size)` when you want Python lists of rows. `batch_size` only tunes
how many rows fastpycsv asks the native parser to process at once while doing
filtered or projected materialization, such as `.filter(...).dicts(...).all()`.
The default is a good starting point; change it only after benchmarking a large
filtered export.

Use `fastpycsv.write_csv()` to stream lazy rows or ordinary Python iterables back
out through csv-parser's native writer:

```python
reader = fastpycsv.reader("vehicles.csv")

fastpycsv.write_csv(
    "cheap_el_paso_fords.csv",
    (row for row in reader if row["region"] == "el paso" and row["manufacturer"] == "ford"),
    fieldnames=["id", "price", "year", "region"],
)
```

Dictionary rows infer their header from the first row when `fieldnames` is not
provided. `None` is written as an empty field; all other Python field values are
stringified.

Pass `cast=True` only when you want csv-parser's scalar classification exposed
as Python values. Empty fields become `None`, boolean fields become `bool`,
integral fields become `int`, floating point fields become `float`, timestamp
fields become `datetime.datetime`, and all other fields remain `str`.

```python
reader = fastpycsv.reader(["id,amount,active\n", "1,2.5,true\n"], cast=True)
rows = list(reader)
assert reader.fieldnames == ["id", "amount", "active"]
assert rows[0].as_list() == [1, 2.5, True]
```

Use `fastpycsv.read_numpy(path, columns=None, *, cast=True, predicate=None)` when
you want eager column arrays suitable for pandas:

```python
import pandas as pd

frame = pd.DataFrame(fastpycsv.read_numpy("data.csv"))
```

`read_numpy()` returns a `dict` keyed by column name. String columns use NumPy
2.x `StringDType`, nullable numeric and boolean columns widen to `float64` with
`NaN`, and non-null integer, float, and boolean columns use dense NumPy arrays.
It does not produce object arrays. The C++ export path batches rows through
`DataFrame` column views and `DataFrameExecutor`; remaining fixed costs are
mostly NumPy `StringDType` construction for string-heavy data and pandas'
DataFrame materialization after the arrays have been built.

Use `fastpycsv.read_numpy_batches(path, columns=None, *, predicate=None, cast=True,
batch_size=50000, schema="sample")` when you want streaming dictionaries of
NumPy arrays instead of one eager full-file result. `schema="sample"` infers
dtypes from the first bounded batch and then streams once, `schema="global"`
does a full pre-scan for stable dtypes matching `read_numpy()`, and
`schema="batch"` infers each emitted batch independently for true one-pass
bounded-memory streaming. With `cast=False`, batches are string-only and skip
schema inference. Explicit `dtypes={column: dtype}` overrides are not implemented
yet and are tracked as a follow-up.

For simple row filters, create native predicates and pass them to
`read_numpy()` or `read_numpy_batches()`:

```python
predicate = fastpycsv.equal("region", "el paso", case_sensitive=False)
arrays = fastpycsv.read_numpy("vehicles.csv", columns=["price", "year"], predicate=predicate)
```

The facade supports the common `delimiter`, `quotechar`, `doublequote=True`,
`skipinitialspace`, `strict`, `consume_header`, and `fieldnames` options.
Unsupported dialect features intentionally fail fast instead of silently
diverging from stdlib behavior.

Lower-level extension objects remain available where they have a clear Python
use, but `fastpycsv` intentionally keeps C++ configuration machinery out of the
stable Python facade.

## Benchmarks

To compare reader throughput locally:

```powershell
python python/benchmarks/compare_readers.py path/to/input.csv
```

To compare the streaming EDA script against pandas using the pyarrow CSV engine:

```powershell
python python/benchmarks/compare_eda.py path/to/input.csv
```

To compare a streaming `fastpycsv` filter against pyarrow on the Used Cars-style
`region == "el paso"` workload:

```powershell
python python/benchmarks/compare_filter.py path/to/vehicles.csv
```

To compare selected-column CSV-to-NumPy materialization against pyarrow:

```powershell
python python/benchmarks/compare_numpy_materialization.py path/to/vehicles.csv
```

To compare ordinary Python object materialization against pyarrow and Polars:

```powershell
python python/benchmarks/compare_python_materialization.py path/to/vehicles.csv
```

This compares full CSV and first+last-column subset materialization for
row-oriented `list[dict]` outputs and column-oriented `dict[str, list]` outputs.

The NumPy benchmark includes pyarrow's direct CSV reader, Dataset/Scanner path,
streaming CSV reader, and Polars lazy scanning. The Dataset row is pyarrow's
closest API for parallel predicate/projection work, but it can require explicit
column types: otherwise pyarrow may infer a narrow integer type from early rows
and later fail on wider values. The benchmark pins obvious numeric columns such
as `price`, `year`, and `odometer` so the comparison measures throughput instead
of schema-inference failure handling.

This runs each workload in a fresh Python process and reports wall time,
throughput, and peak resident/working-set memory.

The benchmark helper searches `build/` and `out/` for a compatible built
`fastpycsv` extension and errors clearly if it is missing. Use the same Python
version that built `fastpycsv`; a `cp310` extension, for example, will not import
under Python 3.14.

The benchmark matrix compares stdlib `csv.reader`, lazy `fastpycsv.reader` rows with
strings, lazy `fastpycsv.reader` rows with `cast=True`, stdlib `csv.DictReader`, raw
`fastpycsv.read_numpy()` array export, and `pandas.DataFrame(fastpycsv.read_numpy(...))`.
It reports file path, size, rows, columns, elapsed seconds, MiB/s, and rows/s.

## Exploratory Summary

For a small dependency-free EDA pass over a CSV:

```powershell
python python/examples/eda_summary.py path/to/input.csv
```

The script streams lazy `fastpycsv` rows once and reports per-column mean, sample
standard deviation, null count, and top observed values. Histograms use a
bounded approximate heavy-hitter sketch by default to keep memory usage stable
on high-cardinality columns. Use `--top-n N` to change the report size,
`--top-capacity N` to change the per-column sketch size, `--exact-values` for
exact value counts, or `--no-header` for headerless files.
