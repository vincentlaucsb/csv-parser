#!/usr/bin/env python3
"""Compare full-record ZIP-contained CSV filtering.

Default workload: count rows where destinationAirport == "PHX" in Dil Wong's
flight prices dataset distributed as a zipped CSV. Use repeated --filter
arguments for multi-column predicates, for example --filter destinationAirport=PHX
--filter totalFare<300.

fastpycsv reads ZIP members through its native ZIP stream. DataFrame libraries
do not have a first-class ZIP-member CSV scan path, so their workers extract the
selected member to a temporary CSV file before invoking their normal CSV reader.
The comparison keeps the full CSV record shape instead of projecting down to
only the filtered columns.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import zipfile
from contextlib import contextmanager
from pathlib import Path, PurePosixPath

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
    run_process,
    selected_workers,
    worker_payload,
)


DEFAULT_FILTER_COLUMN = "destinationAirport"
DEFAULT_FILTER_VALUE = "PHX"


def filters_for_args(args: argparse.Namespace):
    return active_filters(
        args,
        default_column=DEFAULT_FILTER_COLUMN,
        default_value=DEFAULT_FILTER_VALUE,
    )


def zip_member_names(path: Path) -> list[str]:
    with zipfile.ZipFile(path) as archive:
        return [
            name
            for name in archive.namelist()
            if not name.endswith("/")
        ]


def select_zip_member(path: Path, requested: str | None) -> str:
    members = zip_member_names(path)
    if not members:
        raise SystemExit(f"ZIP archive has no file entries: {path}")

    if requested is not None:
        if requested not in members:
            raise SystemExit(f"ZIP member not found: {requested!r}; available members: {members}")
        return requested

    csv_members = [
        name
        for name in members
        if PurePosixPath(name).suffix.lower() == ".csv"
    ]
    if len(csv_members) == 1:
        return csv_members[0]
    if len(csv_members) > 1:
        raise SystemExit(
            "ZIP archive has multiple CSV members; pass --member with one of: "
            + ", ".join(csv_members)
        )

    if len(members) == 1:
        return members[0]

    raise SystemExit(
        "ZIP archive has multiple non-directory members; pass --member with one of: "
        + ", ".join(members)
    )


def zip_member_uncompressed_size(path: Path, member: str) -> int:
    with zipfile.ZipFile(path) as archive:
        return int(archive.getinfo(member).file_size)


@contextmanager
def extracted_zip_member_path(path: Path, member: str):
    handle = tempfile.NamedTemporaryFile(
        "wb",
        suffix=PurePosixPath(member).suffix or ".csv",
        delete=False,
    )
    temp_path = Path(handle.name)
    handle.close()
    try:
        with zipfile.ZipFile(path) as archive:
            with archive.open(member) as source, open(temp_path, "wb") as target:
                while True:
                    chunk = source.read(1024 * 1024)
                    if not chunk:
                        break
                    target.write(chunk)
        yield temp_path
    finally:
        try:
            temp_path.unlink()
        except FileNotFoundError:
            pass


def run_worker(worker: WorkerSpec, args: argparse.Namespace) -> RunResult:
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "--worker",
        worker.worker,
        str(args.zip_file),
        "--delimiter",
        args.delimiter,
    ]
    if args.column is not None:
        command.extend(["--column", args.column, "--value", args.value])
    for spec in args.filters:
        command.extend(["--filter", f"{spec.column}{spec.op}{spec.value}"])
    if args.member is not None:
        command.extend(["--member", args.member])
    if args.case_insensitive:
        command.append("--case-insensitive")
    return run_process(worker.label, command, args.poll_interval, args.timeout)


def print_results(args: argparse.Namespace, results: list[RunResult]) -> None:
    zip_size = args.zip_file.stat().st_size
    zip_size_mib = zip_size / (1024 * 1024)
    member = select_zip_member(args.zip_file, args.member)
    member_size = zip_member_uncompressed_size(args.zip_file, member)
    member_size_mib = member_size / (1024 * 1024)
    print(f"file={args.zip_file}")
    print(f"zip_size={zip_size} bytes ({zip_size_mib:.3f} MiB)")
    print(f"member={member}")
    print(f"member_size={member_size} bytes ({member_size_mib:.3f} MiB)")
    print(f"filters={filter_description(filters_for_args(args))}")
    print(f"warmups={args.warmup_runs} measured_runs={args.runs}")
    print()

    headers = ["Tool", "Status", "MedianSeconds", "MinSeconds", "MaxSeconds", "CSV_MiB/s", "PeakRSSMiB", "Rows", "Columns", "Matches"]
    rows: list[list[str]] = []
    for name, group in group_results(results):
        ok_results = [result for result in group if result.returncode == 0]
        status = "ok" if len(ok_results) == len(group) else f"error({len(group) - len(ok_results)}/{len(group)})"
        seconds = [result.wall_seconds for result in ok_results]
        median_seconds = median(seconds) if seconds else 0.0
        min_seconds = min(seconds) if seconds else 0.0
        max_seconds = max(seconds) if seconds else 0.0
        peak_rss = median([float(result.peak_rss_bytes) for result in ok_results]) if ok_results else 0.0
        payload = worker_payload(ok_results[0].stdout) if ok_results else {}
        throughput = member_size_mib / median_seconds if median_seconds > 0 else 0.0
        rows.append([
            name,
            status,
            f"{median_seconds:.6f}",
            f"{min_seconds:.6f}",
            f"{max_seconds:.6f}",
            f"{throughput:.3f}",
            f"{mib(int(peak_rss)):.1f}",
            str(payload.get("rows", "")),
            str(payload.get("columns", "")),
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
        reader = fastpycsv.reader(
            args.zip_file,
            member=args.member,
            delimiter=args.delimiter,
        )
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

    print(json.dumps({"columns": len(column_names), "matches": matches}, sort_keys=True))


def worker_pyarrow(args: argparse.Namespace) -> None:
    import pyarrow as pa
    import pyarrow.compute as pc
    import pyarrow.csv as pacsv

    filters = filters_for_args(args)
    member = select_zip_member(args.zip_file, args.member)
    with extracted_zip_member_path(args.zip_file, member) as csv_path:
        table = pacsv.read_csv(
            csv_path,
            read_options=pacsv.ReadOptions(use_threads=True),
            parse_options=pacsv.ParseOptions(
                delimiter=args.delimiter,
                newlines_in_values=True,
            ),
        )

    mask = None
    for spec in filters:
        next_mask = pyarrow_filter_mask(pa, pc, table[spec.column], spec, args.case_insensitive)
        mask = next_mask if mask is None else pc.and_(mask, next_mask)
    matches = table.filter(mask).num_rows
    print(json.dumps({"rows": table.num_rows, "columns": table.num_columns, "matches": matches}, sort_keys=True))


def worker_polars(args: argparse.Namespace) -> None:
    import polars as pl

    filters = filters_for_args(args)
    member = select_zip_member(args.zip_file, args.member)
    with extracted_zip_member_path(args.zip_file, member) as csv_path:
        frame = pl.read_csv(
            csv_path,
            separator=args.delimiter,
            infer_schema=False,
        )

    predicate = None
    for spec in filters:
        next_predicate = polars_filter_expr(pl, spec, args.case_insensitive)
        predicate = next_predicate if predicate is None else predicate & next_predicate
    matches = frame.filter(predicate).height
    print(json.dumps({"rows": frame.height, "columns": frame.width, "matches": matches}, sort_keys=True))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zip_file", type=Path)
    parser.add_argument("--member", help="CSV member name inside the ZIP archive")
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
    parser.add_argument("--warmups", dest="warmup_runs", type=int, default=1)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=0.05,
        help="seconds between memory samples",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=None,
        help="maximum seconds to wait for one worker subprocess before killing it",
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
        raise SystemExit("--warmups must be non-negative")
    if args.timeout is not None and args.timeout <= 0:
        raise SystemExit("--timeout must be positive")
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
        WorkerSpec("fastpycsv", "fastpycsv_zip_parallel_predicate_filter", ("fastpycsv",)),
        WorkerSpec("pyarrow", "pyarrow_zip_tempfile_filter", ("pyarrow",)),
        WorkerSpec("polars", "polars_zip_tempfile_filter", ("polars",)),
    ], args.only)
    for run_index in range(args.warmup_runs):
        for worker in workers[run_index % len(workers):] + workers[:run_index % len(workers)]:
            run_worker(worker, args)

    results: list[RunResult] = []
    for run_index in range(args.runs):
        for worker in workers[run_index % len(workers):] + workers[:run_index % len(workers)]:
            results.append(run_worker(worker, args))

    print_results(args, results)


if __name__ == "__main__":
    main()
