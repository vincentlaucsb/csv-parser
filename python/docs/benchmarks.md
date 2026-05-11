# Benchmarks

The Python benchmark helpers live under `python/benchmarks/`.

Compare reader throughput:

```powershell
python python/benchmarks/compare_readers.py path/to/input.csv
```

Compare the streaming EDA example against pandas with the pyarrow CSV engine:

```powershell
python python/benchmarks/compare_eda.py path/to/input.csv
```

Compare a streaming `csvpy` filter against pyarrow on a Used Cars-style
`region == "el paso"` workload:

```powershell
python python/benchmarks/compare_filter.py path/to/vehicles.csv
```

Compare selected-column CSV-to-NumPy materialization against pyarrow:

```powershell
python python/benchmarks/compare_numpy_materialization.py path/to/vehicles.csv
```

Compare Python object materialization against pyarrow and Polars:

```powershell
python python/benchmarks/compare_python_materialization.py path/to/vehicles.csv
```

This compares full CSV and first+last-column subset materialization for
row-oriented `list[dict]` outputs and column-oriented `dict[str, list]` outputs.

The NumPy materialization benchmark includes pyarrow's direct CSV reader,
pyarrow's Dataset/Scanner path, pyarrow's streaming CSV reader, and Polars lazy
scanning. The Dataset row is included because it is pyarrow's closest API for
parallel predicate/projection work, but it is also more configuration-sensitive
than `pyarrow.csv.read_csv()`: without explicit column types, pyarrow Dataset
may infer a narrow integer type from early rows and later fail on wider values.
The benchmark pins obvious numeric columns such as `price`, `year`, and
`odometer` to avoid measuring that crash path instead of CSV/filter throughput.

The helpers run workloads in fresh Python processes and report wall time,
throughput, and peak resident or working-set memory.

Use the same Python version that built `csvpy`; for example, a `cp310`
extension will not import under Python 3.14.
