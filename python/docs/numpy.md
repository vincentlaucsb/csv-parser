# NumPy and pandas

Use `csvpy.read_numpy(path, columns=None, cast=True)` for eager column arrays
suitable for pandas:

```python
import csvpy
import pandas as pd

arrays = csvpy.read_numpy("data.csv")
frame = pd.DataFrame(arrays)
```

`read_numpy()` returns a dictionary keyed by column name.

Column behavior:

- String columns use NumPy 2.x `StringDType`.
- Non-null integer, float, and boolean columns use dense NumPy arrays.
- Nullable numeric and boolean columns widen to `float64` with `NaN`.
- Object arrays are intentionally avoided.

Selected-column reads keep the Python handoff smaller:

```python
arrays = csvpy.read_numpy("vehicles.csv", columns=["price", "year", "odometer"])
```

Use `csvpy.read_numpy_batches()` for streaming dictionaries of NumPy arrays:

```python
for arrays in csvpy.read_numpy_batches(
    "vehicles.csv",
    columns=["price", "year"],
    schema="sample",
):
    consume(arrays)
```

Batch schema modes trade dtype stability against streaming cost:

- `schema="sample"` is the default. It infers from the first bounded batch and
  then streams once with that schema.
- `schema="global"` pre-scans the file to keep inferred dtypes stable across
  all batches, matching `read_numpy()` behavior.
- `schema="batch"` infers each emitted batch independently for true one-pass
  bounded-memory streaming.

`cast=False` returns string-only batches and skips schema inference. Explicit
`dtypes={column: dtype}` overrides are a planned follow-up.

The native export path batches rows through `DataFrame` column views and
`DataFrameExecutor`. Remaining fixed costs are usually NumPy string-array
construction for string-heavy data and pandas materialization after the arrays
have been built.
