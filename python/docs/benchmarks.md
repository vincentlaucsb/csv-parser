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

The helpers run workloads in fresh Python processes and report wall time,
throughput, and peak resident or working-set memory.

Use the same Python version that built `csvpy`; for example, a `cp310`
extension will not import under Python 3.14.

