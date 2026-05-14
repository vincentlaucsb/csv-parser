#!/usr/bin/env python3
"""Compare fastpycsv EDA against pandas with the pyarrow CSV engine.

The parent process launches each workload in a fresh Python interpreter and
samples wall time plus peak resident/working-set memory. The fastpycsv workload uses
the streaming EDA implementation from python/examples/eda_summary.py. The pandas
workload materializes a DataFrame with engine="pyarrow" and then computes a
similar per-column summary.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import _support as bench_support
from _support import RunResult, WorkerSpec


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_PACKAGE_ROOT = REPO_ROOT / "python"
EDA_SCRIPT = PYTHON_PACKAGE_ROOT / "examples" / "eda_summary.py"


def run_worker(worker: str, label: str, args: argparse.Namespace) -> RunResult:
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "--worker",
        worker,
        str(args.csv_file),
        "--top-n",
        str(args.top_n),
        "--top-capacity",
        str(args.top_capacity),
        "--delimiter",
        args.delimiter,
    ]
    if args.no_header:
        command.append("--no-header")
    if args.fastpycsv_exact_values:
        command.append("--fastpycsv-exact-values")

    return bench_support.run_process(label, command, args.poll_interval)


def print_results(path: Path, results: list[RunResult]) -> None:
    size = path.stat().st_size
    size_mib = size / (1024 * 1024)
    print(f"file={path}")
    print(f"size={size} bytes ({size_mib:.3f} MiB)")
    print()
    print("Tool\tStatus\tSeconds\tMiB/s\tPeakRSSMiB")
    for result in results:
        status = "ok" if result.returncode == 0 else f"error({result.returncode})"
        throughput = size_mib / result.wall_seconds if result.wall_seconds > 0 else 0.0
        print(
            f"{result.name}\t{status}\t{result.wall_seconds:.6f}"
            f"\t{throughput:.3f}\t{bench_support.mib(result.peak_rss_bytes):.1f}"
        )

    for result in results:
        if result.returncode != 0:
            print()
            print(f"{result.name} stderr:")
            print(result.stderr.rstrip() or "<empty>")


def _load_eda_summary():
    import importlib.util

    spec = importlib.util.spec_from_file_location("eda_summary", EDA_SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {EDA_SCRIPT}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _finish_fastpycsv_summary(summaries, top_n: int) -> dict[str, int]:
    value_count = 0
    numeric_count = 0
    for summary in summaries:
        numeric_count += summary.numeric.count
        _ = summary.numeric.stdev
        value_count += len(summary.values.most_common(top_n))

    return {"columns": len(summaries), "numeric_values": numeric_count, "reported_values": value_count}


def worker_fastpycsv(args: argparse.Namespace) -> None:
    eda_summary = _load_eda_summary()
    eda_summary.ensure_fastpycsv_available()
    summaries = eda_summary.analyze(
        args.csv_file,
        args.top_n,
        not args.no_header,
        args.delimiter,
        args.fastpycsv_exact_values,
        args.top_capacity,
    )
    print(json.dumps(_finish_fastpycsv_summary(summaries, args.top_n), sort_keys=True))


def worker_pandas_pyarrow(args: argparse.Namespace) -> None:
    import pandas as pd

    header = None if args.no_header else 0
    frame = pd.read_csv(args.csv_file, engine="pyarrow", sep=args.delimiter, header=header)

    numeric_values = 0
    reported_values = 0
    for column in frame.columns:
        series = frame[column]
        if pd.api.types.is_numeric_dtype(series):
            numeric_values += int(series.count())
            _ = series.mean()
            _ = series.std(ddof=1)

        top = series.value_counts(dropna=False).head(args.top_n)
        reported_values += int(len(top))

    print(json.dumps(
        {
            "columns": int(len(frame.columns)),
            "rows": int(len(frame)),
            "numeric_values": numeric_values,
            "reported_values": reported_values,
        },
        sort_keys=True,
    ))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", type=Path)
    parser.add_argument("-n", "--top-n", type=int, default=10)
    parser.add_argument("--top-capacity", type=int, default=1024)
    parser.add_argument("--delimiter", default=",")
    parser.add_argument("--no-header", action="store_true")
    parser.add_argument(
        "--fastpycsv-exact-values",
        action="store_true",
        help="make fastpycsv use exact value counts instead of bounded approximate counts",
    )
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=0.05,
        help="seconds between memory samples",
    )
    parser.add_argument(
        "--only",
        action="append",
        choices=("fastpycsv", "pandas", "pyarrow"),
        help="only benchmark one library family; repeat to include more than one",
    )
    parser.add_argument("--worker", choices=("fastpycsv", "pandas_pyarrow"), help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.top_n < 1:
        raise SystemExit("--top-n must be at least 1")
    if args.top_capacity < args.top_n:
        raise SystemExit("--top-capacity must be greater than or equal to --top-n")
    if len(args.delimiter) != 1:
        raise SystemExit("--delimiter must be a single character")
    return args


def main() -> None:
    args = parse_args()

    if args.worker == "fastpycsv":
        worker_fastpycsv(args)
        return
    if args.worker == "pandas_pyarrow":
        worker_pandas_pyarrow(args)
        return

    workers = bench_support.selected_workers([
        WorkerSpec(
            "fastpycsv",
            "fastpycsv_eda_approx" if not args.fastpycsv_exact_values else "fastpycsv_eda_exact",
            ("fastpycsv",),
        ),
        WorkerSpec("pandas_pyarrow", "pandas_pyarrow_eda", ("pandas", "pyarrow")),
    ], args.only)
    results = [
        run_worker(worker.worker, worker.label, args)
        for worker in workers
    ]
    print_results(args.csv_file, results)


if __name__ == "__main__":
    main()
