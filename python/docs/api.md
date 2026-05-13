# API Reference

This page documents the stable Python-facing API. `reader()`, `read_numpy()`,
`read_numpy_batches()`, and `write_csv()` are the primary surface area. The
lower-level extension objects exist for users who need direct access to parser
concepts, but ordinary ETL code should not start there.

## Primary API

| Function | Use it when |
| --- | --- |
| `fastpycsv.reader()` | You want fast lazy row iteration and Pythonic row filtering. |
| `fastpycsv.read_numpy()` | You want selected CSV columns as eager NumPy arrays. |
| `fastpycsv.read_numpy_batches()` | You want selected CSV columns as bounded-memory NumPy batches. |
| `fastpycsv.write_csv()` | You want to stream lazy rows or Python iterables back to CSV. |

## `fastpycsv.reader(csvfile, dialect="excel", **fmtparams)`

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
- `batch_size`: advanced performance hint for filtered or projected exports.
  Most users can ignore it.

Only the default `excel` dialect is currently supported. Unsupported dialect
features fail fast instead of silently diverging from stdlib behavior.

`batch_size` is not the same thing as `.chunks(size)`. For ordinary lazy
iteration, `reader()` yields one row at a time no matter what `batch_size` is:

```python
for row in fastpycsv.reader("vehicles.csv", batch_size=100_000):
    consume(row)  # still one row at a time
```

Use `.chunks(size)` on `reader.lists()`, `reader.tuples()`, or `reader.dicts()`
when you want Python lists of rows. `batch_size` only controls how many rows
fastpycsv asks the native parser to process at once while doing filtered or
projected materialization, such as `.filter(...).dicts(...).all()`. The default
is a good starting point; tune it only after benchmarking a large filtered
export.

Use `reader.filter(predicate)` to apply native row filtering before lazy or
materialized rows are emitted:

```python
predicate = fastpycsv.all_of(
    fastpycsv.equal("region", "el paso", case_sensitive=False),
    fastpycsv.less("price", "15000"),
)

for row in fastpycsv.reader("vehicles.csv").filter(predicate):
    consume(row)

batches = fastpycsv.reader("vehicles.csv").filter(predicate).dicts(["id", "price"]).chunks(50_000)
```

Predicates are created with `fastpycsv.equal()`, `less()`, `less_equal()`,
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
- `row.as_list(columns=None)`; pass column names to materialize a subset
- `row.as_tuple(columns=None)`; pass column names to materialize a subset
- `row.as_dict(columns=None)`; pass column names to materialize a subset
- typed access helpers: `get_str`, `get_int`, `get_float`, `get_bool`
- `row.type(index)` for native scalar classification

## Materialized Row Iterators

Use these when you want plain Python row objects but still want bounded-memory
streaming:

Each materialized iterator supports:

- normal iteration, yielding one materialized row at a time
- `.chunks(size)`, yielding `list` batches of materialized rows
- `.all()`, consuming the remaining rows into one Python `list`

### `reader.lists(columns=None)`

Use this when the downstream API expects mutable row lists.

```python
import fastpycsv

for row in fastpycsv.reader("vehicles.csv").lists(["id", "price"]):
    assert isinstance(row, list)
    send_to_api(row)
```

### `reader.tuples(columns=None)`

Use this when the downstream API expects fixed-shape rows.

```python
rows = fastpycsv.reader("vehicles.csv").tuples(["id", "year"]).all()

# [('1', '2021'), ('2', '2020'), ...]
```

### `reader.dicts(columns=None)`

Use this when the downstream API wants named fields per row.

```python
rows = fastpycsv.reader("vehicles.csv").dicts(["id", "price"]).all()

# [{'id': '1', 'price': '9000'}, {'id': '2', 'price': '12000'}, ...]
```

### `.chunks(size)`

Use chunks when you want plain Python objects but need bounded peak memory.

```python
for batch in fastpycsv.reader("vehicles.csv").dicts(["id", "price"]).chunks(50_000):
    assert isinstance(batch, list)
    bulk_insert(batch)
```

### Filtering Before Materialization

Native predicates compose with materialized row iterators, so filtering can stay
in C++ before rows become Python objects.

```python
predicate = fastpycsv.all_of(
    fastpycsv.equal("region", "el paso", case_sensitive=False),
    fastpycsv.less("price", 10_000),
)

for batch in (
    fastpycsv.reader("vehicles.csv")
    .filter(predicate)
    .dicts(["id", "price", "region"])
    .chunks(10_000)
):
    send_to_api(batch)
```

### Column Selection

Column selection controls both output order and shape.

```python
reader = fastpycsv.reader("vehicles.csv")

reader.lists(["price", "id"]).all()
# [['9000', '1'], ['12000', '2'], ...]

reader.tuples(["price", "id"]).all()
# [('9000', '1'), ('12000', '2'), ...]

reader.dicts(["price", "id"]).all()
# [{'price': '9000', 'id': '1'}, {'price': '12000', 'id': '2'}, ...]
```

### Bulk Convenience Methods

The older convenience methods `reader.to_lists(columns=None)`,
`reader.to_tuples(columns=None)`, and `reader.to_dicts(columns=None)` are
equivalent to calling `.all()` on the corresponding materialized iterator.

```python
reader = fastpycsv.reader("vehicles.csv")

reader.to_lists(["id", "price"])
# reader.lists(["id", "price"]).all()

reader.to_tuples(["id", "price"])
# reader.tuples(["id", "price"]).all()

reader.to_dicts(["id", "price"])
# reader.dicts(["id", "price"]).all()
```

## `fastpycsv.read_numpy(path, columns=None, *, cast=True, predicate=None, **fmtparams)`

Parses selected columns into NumPy arrays keyed by column name.

Use this when the target is pandas, NumPy, or another column-oriented consumer:

```python
arrays = fastpycsv.read_numpy("vehicles.csv", columns=["price", "year", "odometer"])
```

`predicate` may be a native fastpycsv predicate such as `equal()`, `less()`, or
`all_of()`. With `cast=True`, fastpycsv classifies scalar fields and maps them to
NumPy-friendly column types.

`read_numpy()` accepts the same CSV format keywords as `reader()`, including
`delimiter`, `quotechar`, `skipinitialspace`, `strict`, `consume_header`, and
`fieldnames`.

See [NumPy and pandas](numpy.md) for dtype behavior and batching details.

## `fastpycsv.read_numpy_batches(path, columns=None, *, predicate=None, cast=True, batch_size=50000, schema="sample", **fmtparams)`

Streams dictionaries of NumPy arrays. This is the bounded-memory version of
`read_numpy()`.

```python
for arrays in fastpycsv.read_numpy_batches("vehicles.csv", columns=["price", "year"]):
    consume(arrays)
```

`schema` controls dtype inference:

- `"sample"`: infer once from the first bounded batch, then stream once.
- `"global"`: pre-scan the file for stable full-file dtypes.
- `"batch"`: infer each emitted batch independently.

`read_numpy_batches()` accepts the same CSV format keywords as `reader()` and
`read_numpy()`.

For both NumPy readers, `path` and `columns` may be positional. Filtering,
casting, batching, schema, and CSV format options are keyword-only.

## `fastpycsv.write_csv(csvfile, rows, **options)`

Writes CSV rows to a path-like output file or text file-like object with a
`write()` method.

`rows` may contain lazy `fastpycsv` rows, dictionaries, lists, tuples, or other
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
reader = fastpycsv.reader("vehicles.csv")
fastpycsv.write_csv(
    "subset.csv",
    (row for row in reader if row["region"] == "el paso"),
    fieldnames=["id", "price", "region"],
)

with open("subset.csv", "w", newline="", encoding="utf-8") as out:
    fastpycsv.write_csv(out, [["id", "price"], [1, 9000]], write_header=False)
```

## Low-Level Types

Most users do not need these. They are exposed for compatibility and specialized
inspection:

- `fastpycsv.Reader`
- `fastpycsv.Row`
- `fastpycsv.Field`
- `fastpycsv.DataType`
- `fastpycsv.get_file_info()`
- `fastpycsv.csv_data_types()`
- `fastpycsv.parse_no_header()`

These are useful when you want direct access to underlying parser concepts that
still have a clear Python use. C++ configuration machinery such as `CSVFormat`
and `VariableColumnPolicy` is used internally by the facade, but is intentionally
kept out of the stable Python surface.
