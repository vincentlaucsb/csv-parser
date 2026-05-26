# DataFrame Agent Notes

This folder implements the `csv::DataFrame` family. The public types stay in
namespace `csv`; the folder split is for maintainability, not a namespace move.

## Storage Model

`DataFrame` is row-backed. Its primary storage is `std::vector<CSVRow>` plus
per-row sparse edit overlays. Do not turn normal row/cell access into a
columnar abstraction just to make structural edit implementations symmetric.

The guiding rule is:

> Use the cheapest reliable operation that preserves visible semantics and
> keeps ordinary row access simple.

Current structural edit strategy:

- Row insert/erase mutates row storage, keyed metadata, and sparse overlay slots
  directly.
- Column insert materializes the current visible table through `CSVWriter`,
  reparses it into fresh row storage, and clears sparse overlays because visible
  edits are baked into the rebuilt rows.
- Column erase is a soft delete: visible column names and the
  logical-to-physical column map change, while underlying `CSVRow` storage stays
  intact.

If repeated column erases need cleanup later, prefer an explicit
compaction/materialization API over adding hot-path indirection for all access.

## Editing Rules

- Keep `DataFrameRow::erase()` and `DataFrameColumn::erase()` behavior aligned:
  structural mutation invalidates outstanding row, column, and cell proxies.
- Do not allow erasing a column-keyed frame's key column unless the keyed lookup
  contract is redesigned at the same time.
- When column visibility changes, keep `columns()`, `n_cols()`, `index_of()`,
  row conversion, JSON, writer output, `column()`, and `column_view()` aligned.
- Preserve the original `ColumnNamePolicy` when rebuilding visible column names
  or reparsing materialized rows.
- Sparse overlays are keyed by physical column index. Any feature that changes
  physical row storage or logical-to-physical mapping must account for existing
  overlays.
- DataFrame iterators should follow the library's cached-proxy convention:
  store the current proxy inside the iterator and expose `operator*` /
  `operator->` reference-like access, as `CSVReader` and `CSVRow` do.

## Test Expectations

Put DataFrame behavior tests in `tests/test_data_frame.cpp`. For writer
compatibility, also check `tests/test_write_csv.cpp` when row-like output or
`to_sv_range()` behavior changes.

Structural edit tests should cover:

- keyed and unkeyed frames
- sparse edits before the structural edit
- row conversion / writer output
- JSON output
- column lookup by name and index
- attempts to mutate through const proxies
