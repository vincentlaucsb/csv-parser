@page csv_tuning CSV Tuning

# CSV Tuning

`csv_tuning` is a small benchmark helper for finding a good chunk size and
speculative parser worker count for your machine and dataset.

It is useful when:

- your CSV files are large enough that parser throughput matters
- you want to compare 2, 4, 8, or automatic worker counts
- you want to see whether smaller or larger chunks help a specific workload
- you want speculative parsing diagnostics such as chunk count and repairs

## What It Measures

`csv_tuning` repeatedly parses one input file with a matrix of:

- chunk sizes
- requested speculative parser worker counts
- optional repeated passes

The output is CSV so it can be pasted into a spreadsheet, plotted, or compared
between machines.

Example output columns include:

- `chunk_bytes`
- `requested_threads`
- `parser_threads`
- `seconds`
- `MiB_per_s`
- `rows`
- `columns`
- speculative diagnostics such as `spec_chunks`, `ambiguous`, and `repairs`

## Usage

```sh
csv_tuning large.csv
```

Useful options:

```sh
csv_tuning large.csv --chunks 4M,8M,10M,16M --threads 1,2,4,8,0 --passes 3
csv_tuning large.csv --no-speculative
```

`--threads 0` means "choose automatically", matching
`CSVFormat::speculative_parallel_threads(0)`.

## Interpreting Results

For most workloads, start with the defaults:

- 10MB chunks
- speculative parsing enabled for large files
- automatic worker count

Then look for the smallest configuration that reaches near-peak throughput.
Using every hardware thread is not always best for medium-sized files; fewer
workers may reduce scheduling overhead and leave more CPU for downstream ETL
work.

The library default enables speculative parsing at 50MB when runtime threading
is enabled. You can lower or raise that threshold:

```cpp
csv::CSVFormat format;
format.speculative_parallel_min_bytes(50 * 1024 * 1024)
      .speculative_parallel_threads(4);
```

For tiny CSVs, use `threading(false)` or set a higher threshold. For bulk ETL on
large files, lower thresholds and explicit worker counts may be worth testing.
