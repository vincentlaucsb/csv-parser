#!/usr/bin/env python3
"""Compare best-path CSV filtering across libraries.

Default workload: count rows where region == "el paso", matching the Craigslist
Used Cars dataset. Use repeated --filter arguments for multi-column predicates,
for example --filter region="el paso" --filter price<10000.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

from _support import (
    RunResult,
    WorkerSpec,
    active_filters,
    ensure_fastpycsv_available,
    filter_description,
    group_results,
    median,
    mib,
    native_predicate,
    parse_filter,
    polars_filter_expr,
    print_table,
    pyarrow_filter_mask,
    rotated_workers,
    run_process,
    selected_workers,
    unique_filter_columns,
    worker_payload,
)


DEFAULT_FILTER_COLUMN = "region"
DEFAULT_FILTER_VALUE = "el paso"


def filters_for_args(args: argparse.Namespace):
    return active_filters(
        args,
        default_column=DEFAULT_FILTER_COLUMN,
        default_value=DEFAULT_FILTER_VALUE,
    )


def run_worker(worker: WorkerSpec, args: argparse.Namespace) -> RunResult:
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "--worker",
        worker.worker,
        str(args.csv_file),
        "--delimiter",
        args.delimiter,
    ]
    if args.column is not None:
        command.extend(["--column", args.column, "--value", args.value])
    for spec in args.filters:
        command.extend(["--filter", f"{spec.column}{spec.op}{spec.value}"])
    if args.case_insensitive:
        command.append("--case-insensitive")
    return run_process(worker.label, command, args.poll_interval)


def print_results(path: Path, args: argparse.Namespace, results: list[RunResult]) -> None:
    size = path.stat().st_size
    size_mib = size / (1024 * 1024)
    print(f"file={path}")
    print(f"size={size} bytes ({size_mib:.3f} MiB)")
    print(f"filters={filter_description(filters_for_args(args))}")
    print(f"warmups={args.warmup_runs} measured_runs={args.runs}")
    print()

    headers = ["Tool", "Status", "MedianSeconds", "MinSeconds", "MaxSeconds", "MiB/s", "PeakRSSMiB", "Matches"]
    rows: list[list[str]] = []
    for name, group in group_results(results):
        ok_results = [result for result in group if result.returncode == 0]
        status = "ok" if len(ok_results) == len(group) else f"error({len(group) - len(ok_results)}/{len(group)})"
        seconds = [result.wall_seconds for result in ok_results]
        median_seconds = median(seconds)
        peak_rss = median([float(result.peak_rss_bytes) for result in ok_results]) if ok_results else 0.0
        payload = worker_payload(ok_results[0].stdout) if ok_results else {}
        throughput = size_mib / median_seconds if median_seconds > 0 else 0.0
        rows.append([
            name,
            status,
            f"{median_seconds:.6f}",
            f"{min(seconds):.6f}" if seconds else "0.000000",
            f"{max(seconds):.6f}" if seconds else "0.000000",
            f"{throughput:.3f}",
            f"{mib(peak_rss):.1f}",
            str(payload.get("matches", "")),
        ])

    print_table(headers, rows, left_aligned={0, 1})

    for result in results:
        if result.returncode != 0:
            print()
            print(f"{result.name} stderr:")
            print(result.stderr.rstrip() or "<empty>")


def worker_fastpycsv(args: argparse.Namespace) -> None:
    ensure_fastpycsv_available()
    import fastpycsv

    previous_parallel = os.environ.get("FASTPYCSV_PREDICATE_PARALLEL")
    os.environ["FASTPYCSV_PREDICATE_PARALLEL"] = "1"
    try:
        reader = fastpycsv.reader(args.csv_file, delimiter=args.delimiter)
        column_names = reader.get_col_names()
        predicates = []
        for spec in filters_for_args(args):
            if spec.column not in column_names:
                raise SystemExit(f"column not found: {spec.column!r}; available columns: {column_names}")
            predicates.append(native_predicate(fastpycsv, spec, args.case_insensitive))

        predicate = predicates[0] if len(predicates) == 1 else fastpycsv.all_of(*predicates)
        reader.filter(predicate)
        matches = sum(1 for _ in reader)
    finally:
        if previous_parallel is None:
            os.environ.pop("FASTPYCSV_PREDICATE_PARALLEL", None)
        else:
            os.environ["FASTPYCSV_PREDICATE_PARALLEL"] = previous_parallel

    print(json.dumps({"matches": matches}, sort_keys=True))


def worker_pyarrow(args: argparse.Namespace) -> None:
    import pyarrow as pa
    import pyarrow.compute as pc
    import pyarrow.csv as pacsv

    filters = filters_for_args(args)
    table = pacsv.read_csv(
        str(args.csv_file),
        read_options=pacsv.ReadOptions(use_threads=True),
        parse_options=pacsv.ParseOptions(
            delimiter=args.delimiter,
            newlines_in_values=True,
        ),
        convert_options=pacsv.ConvertOptions(include_columns=unique_filter_columns(filters)),
    )

    mask = None
    for spec in filters:
        next_mask = pyarrow_filter_mask(pa, pc, table[spec.column], spec, args.case_insensitive)
        mask = next_mask if mask is None else pc.and_(mask, next_mask)
    matches = table.filter(mask).num_rows
    print(json.dumps({"matches": matches}, sort_keys=True))


def worker_polars(args: argparse.Namespace) -> None:
    import polars as pl

    predicate = None
    for spec in filters_for_args(args):
        next_predicate = polars_filter_expr(pl, spec, args.case_insensitive)
        predicate = next_predicate if predicate is None else predicate & next_predicate

    frame = (
        pl.scan_csv(str(args.csv_file), separator=args.delimiter)
        .filter(predicate)
        .select(pl.len().alias("matches"))
        .collect()
    )
    print(json.dumps({"matches": int(frame["matches"][0])}, sort_keys=True))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", type=Path)
    parser.add_argument("--column", default=None, help="legacy single equality filter column")
    parser.add_argument("--value", default=DEFAULT_FILTER_VALUE, help="legacy single equality filter value")
    parser.add_argument(
        "--filter",
        dest="filters",
        action="append",
        default=[],
        type=parse_filter,
        help="filter in COLUMN=VALUE, COLUMN<VALUE, COLUMN<=VALUE, COLUMN>VALUE, or COLUMN>=VALUE form; repeat to AND filters",
    )
    parser.add_argument("--delimiter", default=",")
    parser.add_argument("--case-insensitive", action="store_true")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--warmup-runs", type=int, default=1)
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=0.05,
        help="seconds between memory samples",
    )
    parser.add_argument(
        "--only",
        action="append",
        choices=("fastpycsv", "pyarrow", "polars"),
        help="only benchmark one library family; repeat to include more than one",
    )
    parser.add_argument("--worker", choices=("fastpycsv", "pyarrow", "polars"), help=argparse.SUPPRESS)
    args = parser.parse_args()

    if len(args.delimiter) != 1:
        raise SystemExit("--delimiter must be a single character")
    if args.column is None and args.value != DEFAULT_FILTER_VALUE:
        raise SystemExit("--value requires --column")
    if args.runs < 1:
        raise SystemExit("--runs must be at least 1")
    if args.warmup_runs < 0:
        raise SystemExit("--warmup-runs cannot be negative")
    return args


def main() -> None:
    args = parse_args()

    if args.worker == "fastpycsv":
        worker_fastpycsv(args)
        return
    if args.worker == "pyarrow":
        worker_pyarrow(args)
        return
    if args.worker == "polars":
        worker_polars(args)
        return

    workers = selected_workers([
        WorkerSpec("fastpycsv", "fastpycsv_parallel_predicate_filter", ("fastpycsv",)),
        WorkerSpec("pyarrow", "pyarrow_filter", ("pyarrow",)),
        WorkerSpec("polars", "polars_lazy_filter", ("polars",)),
    ], args.only)
    for run_index in range(args.warmup_runs):
        for worker in rotated_workers(workers, run_index):
            run_worker(worker, args)

    results: list[RunResult] = []
    for run_index in range(args.runs):
        for worker in rotated_workers(workers, run_index):
            results.append(run_worker(worker, args))
    print_results(args.csv_file, args, results)


if __name__ == "__main__":
    main()
