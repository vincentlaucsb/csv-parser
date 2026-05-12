#!/usr/bin/env python3
"""Compare Python object materialization paths.

This benchmark intentionally leaves each library's optimized columnar world and
asks for ordinary Python containers. By default, it compares row-list
materialization (`list[list]`), row-tuple materialization (`list[tuple]`),
row-dict materialization (`list[dict]`), and column-oriented materialization
(`dict[str, list]`) for a projected first+last-column subset. Pass
`--include-full` to also run the much heavier full-CSV materialization cases.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import importlib.machinery
import importlib.util
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_PACKAGE_ROOT = REPO_ROOT / "python"
BUILD_ROOTS = (REPO_ROOT / "build", REPO_ROOT / "out")


@dataclass
class RunResult:
    name: str
    returncode: int
    wall_seconds: float
    peak_rss_bytes: int
    stdout: str
    stderr: str


@dataclass(frozen=True)
class WorkerSpec:
    worker: str
    label: str


def _extension_suffixes() -> tuple[str, ...]:
    return tuple(importlib.machinery.EXTENSION_SUFFIXES)


def _is_compatible_extension_name(name: str) -> bool:
    for suffix in _extension_suffixes():
        if not name.endswith(suffix):
            continue

        if suffix == ".pyd" and ".cp" in name:
            continue

        return True

    return False


def _find_built_fastpycsv_extension() -> Path | None:
    candidates: list[Path] = []
    for build_root in BUILD_ROOTS:
        if not build_root.exists():
            continue

        for candidate in build_root.rglob("fastpycsv*"):
            if (
                candidate.is_file()
                and candidate.name.startswith("fastpycsv")
                and _is_compatible_extension_name(candidate.name)
            ):
                candidates.append(candidate)

    if not candidates:
        return None

    return max(candidates, key=lambda path: path.stat().st_mtime)


def _load_fastpycsv_extension_from_build(extension_path: Path) -> None:
    spec = importlib.util.spec_from_file_location("fastpycsv.fastpycsv", extension_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load fastpycsv extension from {extension_path}")

    module = importlib.util.module_from_spec(spec)
    sys.modules["fastpycsv.fastpycsv"] = module
    spec.loader.exec_module(module)


def ensure_fastpycsv_available() -> None:
    if str(PYTHON_PACKAGE_ROOT) not in sys.path:
        sys.path.insert(0, str(PYTHON_PACKAGE_ROOT))

    try:
        import fastpycsv  # noqa: F401
        return
    except ImportError:
        sys.modules.pop("fastpycsv", None)
        sys.modules.pop("fastpycsv.fastpycsv", None)

    extension_path = _find_built_fastpycsv_extension()
    if extension_path is None:
        suffixes = ", ".join(_extension_suffixes())
        raise SystemExit(
            "fastpycsv is not built for this Python interpreter. "
            f"Expected a fastpycsv extension under {BUILD_ROOTS[0]} or {BUILD_ROOTS[1]} "
            f"with suffix: {suffixes}"
        )

    _load_fastpycsv_extension_from_build(extension_path)
    import fastpycsv  # noqa: F401


def _windows_memory_bytes(pid: int) -> int:
    class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
        _fields_ = [
            ("cb", ctypes.c_ulong),
            ("PageFaultCount", ctypes.c_ulong),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    handle = ctypes.windll.kernel32.OpenProcess(0x1010, False, pid)
    if not handle:
        return 0

    try:
        counters = PROCESS_MEMORY_COUNTERS()
        counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
        ok = ctypes.windll.psapi.GetProcessMemoryInfo(
            handle,
            ctypes.byref(counters),
            counters.cb,
        )
        if not ok:
            return 0
        return int(counters.PeakWorkingSetSize)
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


def _linux_memory_bytes(pid: int) -> int:
    try:
        for line in Path(f"/proc/{pid}/status").read_text(encoding="utf-8").splitlines():
            if line.startswith("VmHWM:"):
                return int(line.split()[1]) * 1024
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024
    except OSError:
        return 0
    return 0


def _ps_memory_bytes(pid: int) -> int:
    try:
        proc = subprocess.run(
            ["ps", "-o", "rss=", "-p", str(pid)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return 0

    text = proc.stdout.strip()
    if not text:
        return 0
    try:
        return int(text.splitlines()[-1].strip()) * 1024
    except ValueError:
        return 0


def process_memory_bytes(pid: int) -> int:
    system = platform.system()
    if system == "Windows":
        return _windows_memory_bytes(pid)
    if system == "Linux":
        return _linux_memory_bytes(pid)
    return _ps_memory_bytes(pid)


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

    env = os.environ.copy()
    env["PYTHONPATH"] = (
        str(PYTHON_PACKAGE_ROOT)
        if not env.get("PYTHONPATH")
        else str(PYTHON_PACKAGE_ROOT) + os.pathsep + env["PYTHONPATH"]
    )

    start = time.perf_counter()
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )

    peak = 0
    while process.poll() is None:
        peak = max(peak, process_memory_bytes(process.pid))
        time.sleep(args.poll_interval)

    peak = max(peak, process_memory_bytes(process.pid))
    stdout, stderr = process.communicate()
    wall = time.perf_counter() - start
    return RunResult(worker.label, process.returncode, wall, peak, stdout, stderr)


def _worker_payload(stdout: str) -> dict:
    text = stdout.strip()
    if not text:
        return {}
    try:
        return json.loads(text.splitlines()[-1])
    except json.JSONDecodeError:
        return {}


def _group_results(results: list[RunResult]) -> list[tuple[str, list[RunResult]]]:
    grouped: dict[str, list[RunResult]] = {}
    for result in results:
        grouped.setdefault(result.name, []).append(result)
    return [(name, grouped[name]) for name in grouped]


def _median(values: list[float]) -> float:
    if not values:
        return 0.0
    return statistics.median(values)


def print_table(headers: list[str], rows: list[list[str]], left_aligned: set[int]) -> None:
    widths = [len(header) for header in headers]
    for row in rows:
        for index, value in enumerate(row):
            widths[index] = max(widths[index], len(value))

    def format_cell(index: int, value: str) -> str:
        if index in left_aligned:
            return value.ljust(widths[index])
        return value.rjust(widths[index])

    print("  ".join(format_cell(index, value) for index, value in enumerate(headers)))
    print("  ".join("-" * width for width in widths))
    for row in rows:
        print("  ".join(format_cell(index, value) for index, value in enumerate(row)))


def print_results(path: Path, args: argparse.Namespace, results: list[RunResult]) -> None:
    size = path.stat().st_size
    mib = size / (1024 * 1024)
    rows: list[list[str]] = []

    print(f"file={path}")
    print(f"size={size} bytes ({mib:.3f} MiB)")
    print(f"warmups={args.warmup_runs} measured_runs={args.runs}")
    print()

    for name, group in _group_results(results):
        ok_results = [result for result in group if result.returncode == 0]
        failed_results = [result for result in group if result.returncode != 0]
        payload = _worker_payload(ok_results[0].stdout) if ok_results else {}

        if ok_results:
            seconds = [result.wall_seconds for result in ok_results]
            median_seconds = _median(seconds)
            min_seconds = min(seconds)
            max_seconds = max(seconds)
            peak_rss = max(result.peak_rss_bytes for result in ok_results) / (1024 * 1024)
            status = "ok" if not failed_results else f"mixed({len(failed_results)}/{len(group)})"
            throughput = mib / median_seconds if median_seconds else 0.0
        else:
            median_seconds = 0.0
            min_seconds = 0.0
            max_seconds = 0.0
            peak_rss = 0.0
            status = f"error({len(failed_results)}/{len(group)})"
            throughput = 0.0

        rows.append(
            [
                name,
                status,
                f"{median_seconds:.6f}",
                f"{min_seconds:.6f}",
                f"{max_seconds:.6f}",
                f"{throughput:.3f}",
                f"{peak_rss:.1f}",
                str(payload.get("rows", "")),
                str(payload.get("columns", "")),
                str(payload.get("objects", "")),
            ]
        )

    print_table(
        [
            "Tool",
            "Status",
            "MedianSeconds",
            "MinSeconds",
            "MaxSeconds",
            "MiB/s",
            "PeakRSSMiB",
            "Rows",
            "Columns",
            "Objects",
        ],
        rows,
        {0, 1},
    )

    for result in results:
        if result.returncode == 0:
            continue
        if result.stderr.strip():
            print()
            print(f"{result.name} stderr:")
            print(result.stderr.strip())


def _subset_names(names: list[str]) -> list[str]:
    if not names:
        return []
    if len(names) == 1:
        return [names[0]]
    return [names[0], names[-1]]


def _selected_columns_from_header(args: argparse.Namespace) -> list[str]:
    with open(args.csv_file, newline="", encoding="utf-8") as handle:
        header = next(csv.reader(handle, delimiter=args.delimiter), [])
    return _subset_names(header)


def _pyarrow_parse_options(args: argparse.Namespace, *, multiline: bool):
    import pyarrow.csv as pacsv

    return pacsv.ParseOptions(
        delimiter=args.delimiter,
        newlines_in_values=multiline,
    )


def _pyarrow_read_csv(args: argparse.Namespace, *, subset: bool, multiline: bool):
    import pyarrow.csv as pacsv

    convert_options = None
    if subset:
        convert_options = pacsv.ConvertOptions(include_columns=_selected_columns_from_header(args))
    return pacsv.read_csv(
        args.csv_file,
        parse_options=_pyarrow_parse_options(args, multiline=multiline),
        convert_options=convert_options,
    )


def _emit(rows: int, columns: int, objects: int) -> None:
    print(json.dumps({"rows": rows, "columns": columns, "objects": objects}, sort_keys=True))


def worker_fastpycsv_row_dicts(args: argparse.Namespace, *, subset: bool) -> None:
    ensure_fastpycsv_available()
    import fastpycsv

    with open(args.csv_file, newline="", encoding="utf-8") as handle:
        reader = fastpycsv.reader(handle, delimiter=args.delimiter)
        fieldnames = reader.fieldnames
        selected = _subset_names(fieldnames) if subset else fieldnames
        data = reader.to_dicts(selected if subset else None)
    _emit(len(data), len(selected), len(data))


def worker_fastpycsv_row_lists(args: argparse.Namespace, *, subset: bool) -> None:
    ensure_fastpycsv_available()
    import fastpycsv

    with open(args.csv_file, newline="", encoding="utf-8") as handle:
        reader = fastpycsv.reader(handle, delimiter=args.delimiter)
        fieldnames = reader.fieldnames
        selected = _subset_names(fieldnames) if subset else fieldnames
        data = reader.to_lists(selected if subset else None)
    _emit(len(data), len(selected), len(data))


def worker_fastpycsv_row_tuples(args: argparse.Namespace, *, subset: bool) -> None:
    ensure_fastpycsv_available()
    import fastpycsv

    with open(args.csv_file, newline="", encoding="utf-8") as handle:
        reader = fastpycsv.reader(handle, delimiter=args.delimiter)
        fieldnames = reader.fieldnames
        selected = _subset_names(fieldnames) if subset else fieldnames
        data = reader.to_tuples(selected if subset else None)
    _emit(len(data), len(selected), len(data))


def worker_fastpycsv_column_dict(args: argparse.Namespace, *, subset: bool) -> None:
    ensure_fastpycsv_available()
    import fastpycsv

    with open(args.csv_file, newline="", encoding="utf-8") as handle:
        reader = fastpycsv.reader(handle, delimiter=args.delimiter)
        fieldnames = reader.fieldnames
        selected = _subset_names(fieldnames) if subset else fieldnames
        data = {name: [] for name in selected}
        for row in reader:
            for name in selected:
                data[name].append(row[name])
    rows = len(next(iter(data.values()))) if data else 0
    _emit(rows, len(data), len(data))


def worker_pyarrow_row_dicts(args: argparse.Namespace, *, subset: bool, multiline: bool) -> None:
    table = _pyarrow_read_csv(args, subset=subset, multiline=multiline)
    data = table.to_pylist()
    _emit(len(data), table.num_columns, len(data))


def worker_pyarrow_row_lists(args: argparse.Namespace, *, subset: bool, multiline: bool) -> None:
    table = _pyarrow_read_csv(args, subset=subset, multiline=multiline)
    columns = table.to_pydict()
    values = list(columns.values())
    data = [list(row) for row in zip(*values)] if values else []
    _emit(len(data), table.num_columns, len(data))


def worker_pyarrow_column_dict(args: argparse.Namespace, *, subset: bool, multiline: bool) -> None:
    table = _pyarrow_read_csv(args, subset=subset, multiline=multiline)
    data = table.to_pydict()
    rows = len(next(iter(data.values()))) if data else 0
    _emit(rows, len(data), len(data))


def worker_polars_row_dicts(args: argparse.Namespace, *, subset: bool) -> None:
    import polars as pl

    columns = _selected_columns_from_header(args) if subset else None
    frame = pl.read_csv(args.csv_file, separator=args.delimiter, columns=columns)
    data = frame.to_dicts()
    _emit(len(data), len(frame.columns), len(data))


def worker_polars_row_lists(args: argparse.Namespace, *, subset: bool) -> None:
    import polars as pl

    columns = _selected_columns_from_header(args) if subset else None
    frame = pl.read_csv(args.csv_file, separator=args.delimiter, columns=columns)
    data = [list(row) for row in frame.rows()]
    _emit(len(data), len(frame.columns), len(data))


def worker_polars_row_tuples(args: argparse.Namespace, *, subset: bool) -> None:
    import polars as pl

    columns = _selected_columns_from_header(args) if subset else None
    frame = pl.read_csv(args.csv_file, separator=args.delimiter, columns=columns)
    data = frame.rows()
    _emit(len(data), len(frame.columns), len(data))


def worker_polars_column_dict(args: argparse.Namespace, *, subset: bool) -> None:
    import polars as pl

    columns = _selected_columns_from_header(args) if subset else None
    frame = pl.read_csv(args.csv_file, separator=args.delimiter, columns=columns)
    data = frame.to_dict(as_series=False)
    rows = len(next(iter(data.values()))) if data else 0
    _emit(rows, len(data), len(data))


def full_benchmark_workers() -> list[WorkerSpec]:
    return [
        WorkerSpec("fastpycsv_row_lists", "fastpycsv_to_list_of_lists"),
        WorkerSpec("pyarrow_row_lists", "pyarrow_to_list_of_lists"),
        WorkerSpec("pyarrow_row_lists_multiline", "pyarrow_to_list_of_lists_multiline"),
        WorkerSpec("polars_row_lists", "polars_to_list_of_lists"),
        WorkerSpec("fastpycsv_row_tuples", "fastpycsv_to_list_of_tuples"),
        WorkerSpec("polars_row_tuples", "polars_to_list_of_tuples"),
        WorkerSpec("fastpycsv_row_dicts", "fastpycsv_to_list_of_dicts"),
        WorkerSpec("pyarrow_row_dicts", "pyarrow_to_list_of_dicts"),
        WorkerSpec("pyarrow_row_dicts_multiline", "pyarrow_to_list_of_dicts_multiline"),
        WorkerSpec("polars_row_dicts", "polars_to_list_of_dicts"),
        WorkerSpec("fastpycsv_column_dict", "fastpycsv_to_dict_of_lists"),
        WorkerSpec("pyarrow_column_dict", "pyarrow_to_dict_of_lists"),
        WorkerSpec("pyarrow_column_dict_multiline", "pyarrow_to_dict_of_lists_multiline"),
        WorkerSpec("polars_column_dict", "polars_to_dict_of_lists"),
    ]


def subset_benchmark_workers() -> list[WorkerSpec]:
    return [
        WorkerSpec("fastpycsv_row_lists_subset", "fastpycsv_subset_to_list_of_lists"),
        WorkerSpec("pyarrow_row_lists_subset", "pyarrow_subset_to_list_of_lists"),
        WorkerSpec("pyarrow_row_lists_subset_multiline", "pyarrow_subset_to_list_of_lists_multiline"),
        WorkerSpec("polars_row_lists_subset", "polars_subset_to_list_of_lists"),
        WorkerSpec("fastpycsv_row_tuples_subset", "fastpycsv_subset_to_list_of_tuples"),
        WorkerSpec("polars_row_tuples_subset", "polars_subset_to_list_of_tuples"),
        WorkerSpec("fastpycsv_row_dicts_subset", "fastpycsv_subset_to_list_of_dicts"),
        WorkerSpec("pyarrow_row_dicts_subset", "pyarrow_subset_to_list_of_dicts"),
        WorkerSpec("pyarrow_row_dicts_subset_multiline", "pyarrow_subset_to_list_of_dicts_multiline"),
        WorkerSpec("polars_row_dicts_subset", "polars_subset_to_list_of_dicts"),
        WorkerSpec("fastpycsv_column_dict_subset", "fastpycsv_subset_to_dict_of_lists"),
        WorkerSpec("pyarrow_column_dict_subset", "pyarrow_subset_to_dict_of_lists"),
        WorkerSpec("pyarrow_column_dict_subset_multiline", "pyarrow_subset_to_dict_of_lists_multiline"),
        WorkerSpec("polars_column_dict_subset", "polars_subset_to_dict_of_lists"),
    ]


def all_benchmark_workers() -> list[WorkerSpec]:
    return full_benchmark_workers() + subset_benchmark_workers()


def benchmark_workers(include_full: bool) -> list[WorkerSpec]:
    workers = subset_benchmark_workers()
    if include_full:
        return full_benchmark_workers() + workers
    return workers


def rotated_workers(workers: list[WorkerSpec], offset: int) -> list[WorkerSpec]:
    if not workers:
        return []
    start = offset % len(workers)
    return workers[start:] + workers[:start]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", type=Path)
    parser.add_argument("--delimiter", default=",")
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=0.05,
        help="seconds between memory samples",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=5,
        help="measured subprocess runs per tool",
    )
    parser.add_argument(
        "--warmup-runs",
        type=int,
        default=1,
        help="discarded subprocess warmup runs per tool",
    )
    parser.add_argument(
        "--include-full",
        action="store_true",
        help="also benchmark full-CSV list/dict materialization, which is intentionally expensive",
    )
    parser.add_argument(
        "--worker",
        choices=tuple(worker.worker for worker in all_benchmark_workers()),
        help=argparse.SUPPRESS,
    )
    args = parser.parse_args()

    if len(args.delimiter) != 1:
        raise SystemExit("--delimiter must be a single character")
    if args.runs < 1:
        raise SystemExit("--runs must be at least 1")
    if args.warmup_runs < 0:
        raise SystemExit("--warmup-runs cannot be negative")
    return args


def main() -> None:
    args = parse_args()

    if args.worker == "fastpycsv_row_lists":
        worker_fastpycsv_row_lists(args, subset=False)
        return
    if args.worker == "pyarrow_row_lists":
        worker_pyarrow_row_lists(args, subset=False, multiline=False)
        return
    if args.worker == "pyarrow_row_lists_multiline":
        worker_pyarrow_row_lists(args, subset=False, multiline=True)
        return
    if args.worker == "polars_row_lists":
        worker_polars_row_lists(args, subset=False)
        return
    if args.worker == "fastpycsv_row_tuples":
        worker_fastpycsv_row_tuples(args, subset=False)
        return
    if args.worker == "polars_row_tuples":
        worker_polars_row_tuples(args, subset=False)
        return
    if args.worker == "fastpycsv_row_dicts":
        worker_fastpycsv_row_dicts(args, subset=False)
        return
    if args.worker == "pyarrow_row_dicts":
        worker_pyarrow_row_dicts(args, subset=False, multiline=False)
        return
    if args.worker == "pyarrow_row_dicts_multiline":
        worker_pyarrow_row_dicts(args, subset=False, multiline=True)
        return
    if args.worker == "polars_row_dicts":
        worker_polars_row_dicts(args, subset=False)
        return
    if args.worker == "fastpycsv_column_dict":
        worker_fastpycsv_column_dict(args, subset=False)
        return
    if args.worker == "pyarrow_column_dict":
        worker_pyarrow_column_dict(args, subset=False, multiline=False)
        return
    if args.worker == "pyarrow_column_dict_multiline":
        worker_pyarrow_column_dict(args, subset=False, multiline=True)
        return
    if args.worker == "polars_column_dict":
        worker_polars_column_dict(args, subset=False)
        return
    if args.worker == "fastpycsv_row_lists_subset":
        worker_fastpycsv_row_lists(args, subset=True)
        return
    if args.worker == "pyarrow_row_lists_subset":
        worker_pyarrow_row_lists(args, subset=True, multiline=False)
        return
    if args.worker == "pyarrow_row_lists_subset_multiline":
        worker_pyarrow_row_lists(args, subset=True, multiline=True)
        return
    if args.worker == "polars_row_lists_subset":
        worker_polars_row_lists(args, subset=True)
        return
    if args.worker == "fastpycsv_row_tuples_subset":
        worker_fastpycsv_row_tuples(args, subset=True)
        return
    if args.worker == "polars_row_tuples_subset":
        worker_polars_row_tuples(args, subset=True)
        return
    if args.worker == "fastpycsv_row_dicts_subset":
        worker_fastpycsv_row_dicts(args, subset=True)
        return
    if args.worker == "pyarrow_row_dicts_subset":
        worker_pyarrow_row_dicts(args, subset=True, multiline=False)
        return
    if args.worker == "pyarrow_row_dicts_subset_multiline":
        worker_pyarrow_row_dicts(args, subset=True, multiline=True)
        return
    if args.worker == "polars_row_dicts_subset":
        worker_polars_row_dicts(args, subset=True)
        return
    if args.worker == "fastpycsv_column_dict_subset":
        worker_fastpycsv_column_dict(args, subset=True)
        return
    if args.worker == "pyarrow_column_dict_subset":
        worker_pyarrow_column_dict(args, subset=True, multiline=False)
        return
    if args.worker == "pyarrow_column_dict_subset_multiline":
        worker_pyarrow_column_dict(args, subset=True, multiline=True)
        return
    if args.worker == "polars_column_dict_subset":
        worker_polars_column_dict(args, subset=True)
        return

    workers = benchmark_workers(args.include_full)
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
