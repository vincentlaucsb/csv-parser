# fastpycsv

`fastpycsv` is a fast Python CSV toolkit backed by Vince's CSV Parser. It is built
for ETL-style workflows where users need to scan large CSV files, filter rows,
extract a few columns, materialize NumPy arrays, or write cleaned CSV output
without dragging every workflow through a heavyweight DataFrame engine.

The package is named `fastpycsv`; it does not replace or shadow Python's standard
library `csv` module.

For the native C++ API, see the <a href="../index.html">Vince's CSV Parser documentation</a>.

## Main API

Most users should start with these functions:

```python
import fastpycsv

# Lazy, list-like row objects over the native parser.
for row in fastpycsv.reader("vehicles.csv"):
    if row["region"] == "el paso":
        print(row["price"])

# Materialized Python row objects when another API needs them.
reader = fastpycsv.reader("vehicles.csv")
list_rows = reader.lists(["id", "price"]).all()

tuple_rows = fastpycsv.reader("vehicles.csv").tuples(["id", "price"]).all()
dict_rows = fastpycsv.reader("vehicles.csv").dicts(["id", "price"]).all()

# Column-oriented NumPy arrays for pandas/scientific workflows.
arrays = fastpycsv.read_numpy("vehicles.csv", columns=["price", "year", "odometer"])

# Bounded-memory NumPy batches for large files.
for batch in fastpycsv.read_numpy_batches("vehicles.csv", columns=["price", "year"]):
    consume(batch)

# Native CSV output from lazy rows or ordinary Python iterables.
fastpycsv.write_csv(
    "subset.csv",
    (row for row in fastpycsv.reader("vehicles.csv") if row["region"] == "el paso"),
    fieldnames=["id", "price", "year", "region"],
)
```

## Why fastpycsv?

- **Fast lazy rows:** iterate massive CSV files without eagerly building Python
  lists or dictionaries for every row.
- **Pythonic filtering:** ordinary Python predicates work directly on lazy rows;
  native predicates are available for common comparisons.
- **NumPy export:** selected columns can be materialized into NumPy arrays
  without routing through pandas or pyarrow first.
- **Streaming output:** `write_csv()` lets read/filter/project/write pipelines
  stay bounded-memory.
- **Robust CSV parsing:** embedded newlines and quoted fields are handled by the
  same parser core as the C++ library.

```{toctree}
:maxdepth: 2

quickstart
api
numpy
type-casting
benchmarks
```

## Build These Docs

From the repository root:

```powershell
python -m pip install -r python/docs/requirements.txt
python -m sphinx -b html python/docs docs/html/python
```

The generated HTML lands in `docs/html/python`, matching the GitHub Pages
layout used by the documentation workflow.
