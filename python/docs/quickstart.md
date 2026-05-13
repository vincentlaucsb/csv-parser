# Quickstart

`fastpycsv` is centered around four operations:

- `reader()` for lazy row iteration
- `read_numpy()` for eager selected-column NumPy export
- `read_numpy_batches()` for bounded-memory NumPy export
- `write_csv()` for streaming CSV output

## Read Rows

`fastpycsv.reader()` returns lazy, list-like row objects. By default, the first row
is consumed as column names, so ordinary ETL code can use string indexing
without building a dictionary for every row.

```python
import fastpycsv

for row in fastpycsv.reader("vehicles.csv"):
    if row["region"] == "el paso":
        print(row["price"])
```

Rows can be indexed by position or column name:

```python
row = next(fastpycsv.reader("vehicles.csv"))

row[0]
row["price"]
len(row)
```

Use explicit materialization only when the downstream API needs normal Python
objects:

```python
reader = fastpycsv.reader("vehicles.csv")

rows = reader.lists(["id", "price", "year"]).all()
```

For bounded memory, stream materialized batches:

```python
for rows in fastpycsv.reader("vehicles.csv").dicts(["id", "price"]).chunks(50_000):
    send_to_api(rows)
```

## Export NumPy Arrays

Use `read_numpy()` when the target is pandas, NumPy, or another column-oriented
consumer:

```python
import pandas as pd

arrays = fastpycsv.read_numpy("vehicles.csv", columns=["price", "year", "odometer"])
frame = pd.DataFrame(arrays)
```

Use `read_numpy_batches()` when the file is large enough that peak memory
matters:

```python
for arrays in fastpycsv.read_numpy_batches(
    "vehicles.csv",
    columns=["price", "year", "odometer"],
    schema="sample",
):
    process(arrays)
```

## Filter With Native Predicates

Python predicates are fine for flexible business logic. For common comparisons,
native predicates avoid repeated Python callbacks:

```python
predicate = fastpycsv.all_of(
    fastpycsv.equal("manufacturer", "ford", case_sensitive=False),
    fastpycsv.less("price", 10_000),
)

arrays = fastpycsv.read_numpy(
    "vehicles.csv",
    columns=["region", "price", "year", "odometer"],
    predicate=predicate,
)
```

Chaining `reader.filter(...)` combines native predicates with `all_of()` by
default. Use `append=False` when a later filter should replace the earlier one.

## Write CSV Output

`write_csv()` accepts a path or text file-like object plus lazy rows,
dictionaries, lists, tuples, and other Python iterables. Fields are stringified
before writing; `None` becomes an empty CSV field.

```python
reader = fastpycsv.reader("vehicles.csv")

fastpycsv.write_csv(
    "cheap_el_paso_fords.csv",
    (row for row in reader if row["region"] == "el paso" and row["manufacturer"] == "ford"),
    fieldnames=["id", "price", "year", "region"],
)

with open("cheap_el_paso_fords.csv", "w", newline="", encoding="utf-8") as out:
    fastpycsv.write_csv(out, [["id", "price"], [1, 9000]], write_header=False)
```

## Installation And Local Builds

Install from the repository root while developing:

```powershell
python -m pip install -e E:\GitHub\csv-parser
```

Or build the native extension directly:

```powershell
cmake -S . -B build/fastpycsv -DBUILD_PYTHON=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/fastpycsv --target fastpycsv --config Release
```

For an existing top-level build, the `fastpycsv` target bootstraps its own Python
build tree:

```powershell
cmake --build build/x64-Release --target fastpycsv --config Release
```
