# DataFrame Agent Notes

This folder implements the `csv::DataFrame` family. The public types stay in
namespace `csv`; the folder split is for maintainability, not a namespace move.

## Storage Model

`DataFrame` is row-backed. Its primary storage is `std::vector<CSVRow>`.
Private `internals::data_frame::DirtyDataFrame` owns sparse edit overlays,
logical-to-physical column mapping, and the atomic dirty-state flag. Clean
reads access parsed rows directly, without overlay slots or column mapping.
Do not turn normal row/cell access into a
columnar abstraction just to make structural edit implementations symmetric.

The guiding rule is:

> Use the cheapest reliable operation that preserves visible semantics and
> keeps ordinary row access simple.

Current structural edit strategy:

- Cell assignment activates dirty handling lazily. Taking mutable row/cell
  proxies does not allocate overlays or activate the flag.
- Row insert/erase mutates row storage and keyed metadata directly. The handler
  updates overlay slots when active and tracks the current row-storage base.
- Column insert builds fresh owned row storage from visible values, without CSV
  serialization or reparsing. Shared backing chunks avoid allocation per row
  and preserve empty values and zero-column row cardinality. Successful
  materialization clears dirty handling because visible edits are baked in.
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
- Stored keys identify rows independently of key-column cell edits. Structural
  materialization must preserve those keys, including custom-function keys.
- `csv::string_view` keys must be copied into private owned key storage before
  replacing parsed rows, and the cached key index must be invalidated when views
  are retargeted. Owning-key types use a compile-time no-op storage policy.
- Row proxies resolve current editing state when constructing cells; never
  capture overlay allocation history as the authority for later row access.
- Selection shares parsed rows but snapshots overlays independently and keeps
  the visible column mapping. Never reconstruct a logical frame solely from
  `get_underlying_row()`: it omits edits and includes hidden columns.
- Dirty-state publication and first-overlay creation are synchronized. Keep
  structural changes exclusive; ordinary cell edits retain row-level locking.
- DataFrame iterators should follow the library's cached-proxy convention:
  store the current proxy inside the iterator and expose `operator*` /
  `operator->` reference-like access, as `CSVReader` and `CSVRow` do.

## Test Expectations

Put general DataFrame behavior tests in `tests/test_data_frame.cpp`; put
clean/dirty transitions and edit-preservation regressions in
`tests/test_data_frame_dirty.cpp`. Both belong to the `data_frame_test` target.
For writer
compatibility, also check `tests/test_write_csv.cpp` when row-like output or
`to_sv_range()` behavior changes.

Structural edit tests should cover:

- keyed and unkeyed frames
- sparse edits before the structural edit
- row conversion / writer output
- JSON output
- column lookup by name and index
- attempts to mutate through const proxies
