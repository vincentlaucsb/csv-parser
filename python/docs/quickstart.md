# Quickstart

`csvpy` is centered around four operations:

- `reader()` for lazy row iteration
- `read_numpy()` for eager selected-column NumPy export
- `read_numpy_batches()` for bounded-memory NumPy export
- `write_csv()` for streaming CSV output

## Read Rows

`csvpy.reader()` returns lazy, list-like row objects. By default, the first row
is consumed as column names, so ordinary ETL code can use string indexing
without building a dictionary for every row.

```python
import csvpy

for row in csvpy.reader("vehicles.csv"):
    if row["region"] == "el paso":
        print(row["price"])
```

Rows can be indexed by position or column name:

```python
row = next(csvpy.reader("vehicles.csv"))

row[0]
row["price"]
len(row)
```

Use explicit materialization only when the downstream API needs normal Python
objects:

```python
reader = csvpy.reader("vehicles.csv")

rows = reader.lists(["id", "price", "year"]).all()
```

For bounded memory, stream materialized batches:

```python
for rows in csvpy.reader("vehicles.csv").dicts(["id", "price"]).chunks(50_000):
    send_to_api(rows)
```

## Export NumPy Arrays

Use `read_numpy()` when the target is pandas, NumPy, or another column-oriented
consumer:

```python
import pandas as pd

arrays = csvpy.read_numpy("vehicles.csv", columns=["price", "year", "odometer"])
frame = pd.DataFrame(arrays)
```

Use `read_numpy_batches()` when the file is large enough that peak memory
matters:

```python
for arrays in csvpy.read_numpy_batches(
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
predicate = csvpy.all_of(
    csvpy.equal("manufacturer", "ford", case_sensitive=False),
    csvpy.less("price", 10_000),
)

arrays = csvpy.read_numpy(
    "vehicles.csv",
    columns=["region", "price", "year", "odometer"],
    predicate=predicate,
)
```

Chaining `reader.filter(...)` combines native predicates with `all_of()` by
default. Use `append=False` when a later filter should replace the earlier one.

## Write CSV Output

`write_csv()` accepts lazy rows, dictionaries, lists, tuples, and other Python
iterables. Fields are stringified before writing; `None` becomes an empty CSV
field.

```python
reader = csvpy.reader("vehicles.csv")

csvpy.write_csv(
    "cheap_el_paso_fords.csv",
    (row for row in reader if row["region"] == "el paso" and row["manufacturer"] == "ford"),
    fieldnames=["id", "price", "year", "region"],
)
```

## Installation And Local Builds

Install from the repository root while developing:

```powershell
python -m pip install -e E:\GitHub\csv-parser
```

Or build the native extension directly:

```powershell
cmake -S . -B build/csvpy -DBUILD_PYTHON=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/csvpy --target csvpy --config Release
```

For an existing top-level build, the `csvpy` target bootstraps its own Python
build tree:

```powershell
cmake --build build/x64-Release --target csvpy --config Release
```
