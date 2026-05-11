# API Reference

This page covers the stable Python-facing API. The lower-level extension types
remain available for advanced users, but most code should start with the
stdlib-adjacent facade.

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

## Lazy Row Objects

Rows returned by `reader()` support:

- integer indexing: `row[0]`
- column-name indexing when headers are available: `row["name"]`
- iteration: `list(row)`
- `len(row)`
- `row.as_list()`
- `row.as_dict(columns=None)`; pass column names to materialize a subset
- typed access helpers: `get_str`, `get_int`, `get_float`, `get_bool`
- `row.type(index)` for native scalar classification

## Low-Level Types

The extension also exposes:

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
