# TODO

## csvpy API Cleanup

1. [x] Let numeric predicate factories accept normal Python scalar values.
   - Predicate factories accept Python strings, ints, and floats, then normalize internally.

2. [x] Add projected list materialization for lazy rows.
   - `row.as_list(columns=None)`, `row.as_tuple(columns=None)`, and `row.as_dict(columns=None)` support selected columns.

3. Add Python facade wrappers around NumPy readers.
   - `read_numpy(path, columns=None, cast=True, predicate=None)` and
     `read_numpy_batches(path, columns=None, predicate=None, cast=True, ...)`
     disagree on positional argument order.
   - Make `cast`, `predicate`, `batch_size`, and `schema` keyword-only at the Python layer.

4. Decide how much format control NumPy export should expose.
   - `reader()` supports delimiter, quote character, header behavior, and related options.
   - `read_numpy()` and `read_numpy_batches()` currently use `CSVFormat::guess_csv()`.
   - Either document that difference clearly or add matching keyword options.

5. Clarify `reader(batch_size=...)` documentation.
   - The setting affects native filtered reader batches.
   - Unfiltered lazy iteration still follows `CSVReader::read_row()` behavior.

6. [x] Support path-like and text file-like output targets for `write_csv()`.
   - `write_csv()` accepts paths plus objects with a `write()` method, matching the useful stdlib writer shape.

7. Decide whether predicate clearing is public API.
   - Native `filter(None)` clears the current predicate.
   - If this is useful, document it or expose a named `clear_filter()` helper.
