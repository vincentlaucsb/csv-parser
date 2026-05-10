# API Reference

This page covers the stable Python-facing API. The lower-level extension types
remain available for advanced users, but most code should start with the
stdlib-like facade.

## `csvpy.reader(csvfile, dialect="excel", **fmtparams)`

Returns an iterator over lazy row objects.

Supported formatting options:

- `delimiter`: one-character delimiter. Defaults to `","`.
- `quotechar`: one-character quote character, or `None` to disable quoting.
- `doublequote`: must be `True`.
- `skipinitialspace`: trim leading spaces after delimiters.
- `strict`: throw on variable-width rows.
- `cast`: return Python scalar values instead of strings.
- `typed`: alias for `cast`.
- `batch_size`: row batch size used by the native reader.

Only the default `excel` dialect is currently supported. Unsupported dialect
features fail fast instead of silently diverging from stdlib behavior.

## `csvpy.rows(csvfile, dialect="excel", **fmtparams)`

Like `reader()`, but supports `fieldnames` for attaching column names to lazy
rows without consuming a header row from the input.

## `csvpy.DictReader(csvfile, fieldnames=None, **fmtparams)`

Returns dictionaries keyed by column name. If `fieldnames` is omitted, the first
row is consumed as the header.

The facade supports `restkey` and `restval` for rows with more or fewer fields
than the header list.

## Lazy Row Objects

Rows returned by `reader()` and `rows()` support:

- integer indexing: `row[0]`
- iteration: `list(row)`
- `len(row)`
- `row.as_list()`
- `row.as_dict()`
- typed access helpers: `get_str`, `get_int`, `get_float`, `get_bool`
- `row.type(index)` for native scalar classification

## Low-Level Types

The extension also exposes:

- `csvpy.Format`
- `csvpy.Reader`
- `csvpy.Row`
- `csvpy.Field`
- `csvpy.DataType`
- `csvpy.VariableColumnPolicy`
- `csvpy.get_file_info()`
- `csvpy.csv_data_types()`
- `csvpy.parse()`
- `csvpy.parse_no_header()`

These are useful when you want direct access to the underlying parser concepts
instead of the stdlib-like facade.

