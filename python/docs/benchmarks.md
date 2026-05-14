# Benchmarks

The Python benchmark helpers live under `python/benchmarks/`.

Compare lazy row-reader throughput. This script intentionally excludes NumPy,
pandas, and table materialization workloads:

```powershell
python python/benchmarks/compare_readers.py path/to/input.csv
```

Run the streaming EDA example against pandas with the pyarrow CSV engine:

```powershell
python python/benchmarks/compare_eda.py path/to/input.csv
```

The EDA script is included as an example of bounded-memory row streaming, not as
a claim that arbitrary Python EDA code should beat DataFrame libraries.
Per-row Python aggregation is GIL-bound and will usually lose to pandas,
pyarrow, or Polars for DataFrame-style summaries. Treat this benchmark as a
memory/ergonomics sanity check for streaming workflows.

Compare best-path CSV filtering across fastpycsv, pyarrow, and Polars on a Used
Cars-style `region == "el paso"` workload:

```powershell
python python/benchmarks/compare_filter.py path/to/vehicles.csv
```

Use repeated `--filter` arguments for multi-column filters. Quote filters that
contain `<` or `>` in PowerShell:

```powershell
python python/benchmarks/compare_filter.py path/to/vehicles.csv --filter region="el paso" --filter 'price<10000'
```

The filter head-to-head scripts intentionally show one best available path per
library: fastpycsv uses native predicates with parallel predicate evaluation,
pyarrow uses CSV projection plus compute kernels, and Polars uses lazy/eager
filter expressions as appropriate for plain CSV vs ZIP member inputs.

Compare ZIP-contained CSV filtering across the same library set:

```powershell
python python/benchmarks/compare_zip_filter.py path/to/flight_prices.zip
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

The subprocess-based helpers resolve the local `fastpycsv` extension once in
the parent process and pass it to workers through `FASTPYCSV_EXTENSION_PATH`, so
recursive build-tree discovery is not included in worker timings. They report
wall time, throughput, and peak resident or working-set memory.

Use the same Python version that built `fastpycsv`; for example, a `cp310`
extension will not import under Python 3.14.
