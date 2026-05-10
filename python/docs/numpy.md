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

The native export path batches rows through `DataFrame` column views and
`DataFrameExecutor`. Remaining fixed costs are usually NumPy string-array
construction for string-heavy data and pandas materialization after the arrays
have been built.

