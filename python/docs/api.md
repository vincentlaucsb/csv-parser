# API Reference

This page documents the stable Python-facing API. `reader()`, `read_numpy()`,
`read_numpy_batches()`, and `write_csv()` are the primary surface area. The
lower-level extension objects exist for users who need direct access to parser
concepts, but ordinary ETL code should not start there.

## Primary API

| Function | Use it when |
| --- | --- |
| `csvpy.reader()` | You want fast lazy row iteration and Pythonic row filtering. |
| `csvpy.read_numpy()` | You want selected CSV columns as eager NumPy arrays. |
| `csvpy.read_numpy_batches()` | You want selected CSV columns as bounded-memory NumPy batches. |
| `csvpy.write_csv()` | You want to stream lazy rows or Python iterables back to CSV. |

## `csvpy.reader(csvfile, dialect="excel", **fmtparams)`

Returns an iterator over lazy row objects.

By default, `reader()` consumes the first row as column names. Use
`reader.fieldnames` or `reader.get_col_names()` to retrieve those names before
or during iteration.

Pass `consume_header=False` for raw stdlib-style row iteration where the first
input row is emitted as data. Pass `fieldnames=[...]` to attach explicit column
names without consuming the first input row.

Supported formatting options:

- `delimiter`: one-character delimiter. Defaults to `","`.
- `quotechar`: one-character quote character, or `None` to disable quoting.
- `doublequote`: must be `True`.
- `skipinitialspace`: trim leading spaces after delimiters.
- `strict`: throw on variable-width rows.
- `cast`: return Python scalar values instead of strings.
- `typed`: alias for `cast`.
- `consume_header`: consume the first row as column names. Defaults to `True`.
- `fieldnames`: explicit column names. When provided, the first row is not
  consumed.
- `batch_size`: row batch size used by the native reader.

Only the default `excel` dialect is currently supported. Unsupported dialect
features fail fast instead of silently diverging from stdlib behavior.

Use `reader.filter(predicate)` to apply native row filtering before lazy or
materialized rows are emitted:

```python
predicate = csvpy.all_of(
    csvpy.equal("region", "el paso", case_sensitive=False),
    csvpy.less("price", "15000"),
)

for row in csvpy.reader("vehicles.csv").filter(predicate):
    consume(row)

batches = csvpy.reader("vehicles.csv").filter(predicate).dicts(["id", "price"]).chunks(50_000)
```

Predicates are created with `csvpy.equal()`, `less()`, `less_equal()`,
`greater()`, `greater_equal()`, and `all_of()`. Predicate column names are
validated once against the reader's column names before iteration continues.
Calling `filter()` more than once combines predicates with `all_of()` by
default. Pass `append=False` to replace the current predicate:

```python
reader = reader.filter(new_predicate, append=False)
```

## Lazy Row Objects

Rows returned by `reader()` support:

- integer indexing: `row[0]`
- column-name indexing when headers are available: `row["name"]`
- iteration: `list(row)`
- `len(row)`
- `row.as_list()`
- `row.as_tuple(columns=None)`; pass column names to materialize a subset
- `row.as_dict(columns=None)`; pass column names to materialize a subset
- typed access helpers: `get_str`, `get_int`, `get_float`, `get_bool`
- `row.type(index)` for native scalar classification

## Materialized Row Iterators

Use these when you want plain Python row objects but still want bounded-memory
streaming:

- `reader.lists(columns=None)` yields `list` rows
- `reader.tuples(columns=None)` yields `tuple` rows
- `reader.dicts(columns=None)` yields `dict` rows keyed by column name

Each materialized iterator also supports:

- `.chunks(size)`, yielding `list` batches of materialized rows
- `.all()`, consuming the remaining rows into one Python `list`

The older convenience methods `reader.to_lists(columns=None)`,
`reader.to_tuples(columns=None)`, and `reader.to_dicts(columns=None)` are
equivalent to calling `.all()` on the corresponding materialized iterator.

## `csvpy.read_numpy(path, columns=None, cast=True, predicate=None)`

Parses selected columns into NumPy arrays keyed by column name.

Use this when the target is pandas, NumPy, or another column-oriented consumer:

```python
arrays = csvpy.read_numpy("vehicles.csv", columns=["price", "year", "odometer"])
```

`predicate` may be a native csvpy predicate such as `equal()`, `less()`, or
`all_of()`. With `cast=True`, csvpy classifies scalar fields and maps them to
NumPy-friendly column types.

See [NumPy and pandas](numpy.md) for dtype behavior and batching details.

## `csvpy.read_numpy_batches(path, columns=None, predicate=None, cast=True, batch_size=50000, schema="sample")`

Streams dictionaries of NumPy arrays. This is the bounded-memory version of
`read_numpy()`.

```python
for arrays in csvpy.read_numpy_batches("vehicles.csv", columns=["price", "year"]):
    consume(arrays)
```

`schema` controls dtype inference:

- `"sample"`: infer once from the first bounded batch, then stream once.
- `"global"`: pre-scan the file for stable full-file dtypes.
- `"batch"`: infer each emitted batch independently.

## `csvpy.write_csv(csvfile, rows, **options)`

Writes CSV rows to a path-like output file.

`rows` may contain lazy `csvpy` rows, dictionaries, lists, tuples, or other
Python iterables. Fields are stringified before writing; `None` becomes an empty
field.

Supported options:

- `fieldnames`: optional output column names. When writing dictionaries, this
  controls output order and selection. When omitted for dictionary rows, names
  are inferred from the first row.
- `write_header`: write `fieldnames` as the first output row. Defaults to
  `True`.
- `quote_minimal`: quote only fields that require escaping. Defaults to `True`.

Example:

```python
reader = csvpy.reader("vehicles.csv")
csvpy.write_csv(
    "subset.csv",
    (row for row in reader if row["region"] == "el paso"),
    fieldnames=["id", "price", "region"],
)
```

## Low-Level Types

Most users do not need these. They are exposed for compatibility and specialized
inspection:

- `csvpy.Reader`
- `csvpy.Row`
- `csvpy.Field`
- `csvpy.DataType`
- `csvpy.get_file_info()`
- `csvpy.csv_data_types()`
- `csvpy.parse_no_header()`

These are useful when you want direct access to underlying parser concepts that
still have a clear Python use. C++ configuration machinery such as `CSVFormat`
and `VariableColumnPolicy` is used internally by the facade, but is intentionally
kept out of the stable Python surface.
