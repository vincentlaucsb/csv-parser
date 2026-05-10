#!/usr/bin/env python3
"""Compare selected-column CSV-to-NumPy materialization.

The benchmark normalizes both tools to a dictionary-like set of NumPy arrays for
the same selected columns. With --filter-column/--filter-value, it first filters
rows and then materializes the selected columns. The parent process launches
each tool in a fresh Python interpreter and samples wall time plus peak
resident/working-set memory.
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
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_PACKAGE_ROOT = REPO_ROOT / "python"
BUILD_ROOTS = (REPO_ROOT / "build", REPO_ROOT / "out")
DEFAULT_COLUMNS = ("region", "price", "year", "odometer")


@dataclass
class RunResult:
    name: str
    returncode: int
    wall_seconds: float
    peak_rss_bytes: int
    stdout: str
    stderr: str


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


def _find_built_csvpy_extension() -> Path | None:
    candidates: list[Path] = []
    for build_root in BUILD_ROOTS:
        if not build_root.exists():
            continue

        for candidate in build_root.rglob("csvpy*"):
            if (
                candidate.is_file()
                and candidate.name.startswith("csvpy")
                and _is_compatible_extension_name(candidate.name)
            ):
                candidates.append(candidate)

    if not candidates:
        return None

    return max(candidates, key=lambda path: path.stat().st_mtime)


def _load_csvpy_extension_from_build(extension_path: Path) -> None:
    spec = importlib.util.spec_from_file_location("csvpy.csvpy", extension_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load csvpy extension from {extension_path}")

    module = importlib.util.module_from_spec(spec)
    sys.modules["csvpy.csvpy"] = module
    spec.loader.exec_module(module)


def ensure_csvpy_available() -> None:
    if str(PYTHON_PACKAGE_ROOT) not in sys.path:
        sys.path.insert(0, str(PYTHON_PACKAGE_ROOT))

    try:
        import csvpy  # noqa: F401
        return
    except ImportError:
        sys.modules.pop("csvpy", None)
        sys.modules.pop("csvpy.csvpy", None)

    extension_path = _find_built_csvpy_extension()
    if extension_path is None:
        suffixes = ", ".join(_extension_suffixes())
        raise SystemExit(
            "csvpy is not built for this Python interpreter. "
            f"Expected a csvpy extension under {BUILD_ROOTS[0]} or {BUILD_ROOTS[1]} "
            f"with suffix: {suffixes}"
        )

    _load_csvpy_extension_from_build(extension_path)
    import csvpy  # noqa: F401


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


def run_worker(worker: str, label: str, args: argparse.Namespace) -> RunResult:
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "--worker",
        worker,
        str(args.csv_file),
        "--delimiter",
        args.delimiter,
    ]
    for column in args.columns:
        command.extend(["--columns", column])
    if args.filter_column is not None:
        command.extend(["--filter-column", args.filter_column])
        command.extend(["--filter-value", args.filter_value])
    if args.case_insensitive:
        command.append("--case-insensitive")

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
    return RunResult(label, process.returncode, wall, peak, stdout, stderr)


def mib(value: int) -> float:
    return value / (1024 * 1024)


def _worker_payload(stdout: str) -> dict:
    try:
        return json.loads(stdout.splitlines()[-1])
    except (IndexError, json.JSONDecodeError):
        return {}


def print_results(path: Path, args: argparse.Namespace, results: list[RunResult]) -> None:
    size = path.stat().st_size
    size_mib = size / (1024 * 1024)
    print(f"file={path}")
    print(f"size={size} bytes ({size_mib:.3f} MiB)")
    print(f"columns={', '.join(args.columns)}")
    if args.filter_column is not None:
        print(f"filter={args.filter_column!r} == {args.filter_value!r}")
    print()
    print("Tool\tStatus\tSeconds\tMiB/s\tPeakRSSMiB\tRows\tColumns\tArrayBytes")
    for result in results:
        payload = _worker_payload(result.stdout)
        status = "ok" if result.returncode == 0 else f"error({result.returncode})"
        throughput = size_mib / result.wall_seconds if result.wall_seconds > 0 else 0.0
        print(
            f"{result.name}\t{status}\t{result.wall_seconds:.6f}"
            f"\t{throughput:.3f}\t{mib(result.peak_rss_bytes):.1f}"
            f"\t{payload.get('rows', '')}\t{payload.get('columns', '')}"
            f"\t{payload.get('array_bytes', '')}"
        )

    for result in results:
        if result.returncode != 0:
            print()
            print(f"{result.name} stderr:")
            print(result.stderr.rstrip() or "<empty>")


def _array_nbytes(array: object) -> int:
    return int(getattr(array, "nbytes", 0))


def _header(path: Path, delimiter: str) -> list[str]:
    with path.open(newline="", encoding="utf-8") as handle:
        try:
            return next(csv.reader(handle, delimiter=delimiter))
        except StopIteration as exc:
            raise SystemExit("empty CSV file") from exc


def _column_index(columns: list[str], column: str) -> int:
    try:
        return columns.index(column)
    except ValueError as exc:
        raise SystemExit(f"column not found: {column!r}; available columns: {columns}") from exc


def _string_dtype():
    import numpy as np

    try:
        return np.dtypes.StringDType()
    except AttributeError as exc:
        raise SystemExit("string columns require NumPy 2.x with np.dtypes.StringDType") from exc


def _values_to_numpy(values: list[object]):
    import numpy as np

    non_null = [value for value in values if value is not None]
    if not non_null:
        return np.asarray([np.nan] * len(values), dtype=np.float64)

    if all(isinstance(value, bool) for value in non_null):
        if len(non_null) == len(values):
            return np.asarray(values, dtype=np.bool_)
        return np.asarray([np.nan if value is None else float(value) for value in values], dtype=np.float64)

    if all(isinstance(value, int) and not isinstance(value, bool) for value in non_null):
        if len(non_null) == len(values):
            return np.asarray(values, dtype=np.int64)
        return np.asarray([np.nan if value is None else float(value) for value in values], dtype=np.float64)

    if all(
        (isinstance(value, int) and not isinstance(value, bool)) or isinstance(value, float)
        for value in non_null
    ):
        return np.asarray([np.nan if value is None else float(value) for value in values], dtype=np.float64)

    return np.asarray(["" if value is None else str(value) for value in values], dtype=_string_dtype())


def worker_csvpy(args: argparse.Namespace) -> None:
    ensure_csvpy_available()
    import csvpy

    if args.filter_column is None:
        arrays = csvpy.read_numpy(str(args.csv_file), columns=args.columns)
    else:
        header = _header(args.csv_file, args.delimiter)
        selected_indices = [_column_index(header, column) for column in args.columns]
        filter_index = _column_index(header, args.filter_column)
        expected = args.filter_value.lower() if args.case_insensitive else args.filter_value
        collected: dict[str, list[object]] = {column: [] for column in args.columns}

        for row in csvpy.rows(args.csv_file, delimiter=args.delimiter, cast=True):
            value = row[filter_index]
            if args.case_insensitive:
                value = str(value).lower()
            if value != expected:
                continue

            for column, index in zip(args.columns, selected_indices):
                collected[column].append(row[index])

        arrays = {
            column: _values_to_numpy(values)
            for column, values in collected.items()
        }

    rows = len(next(iter(arrays.values()))) if arrays else 0
    array_bytes = sum(_array_nbytes(array) for array in arrays.values())
    print(
        json.dumps(
            {
                "array_bytes": array_bytes,
                "columns": len(arrays),
                "rows": rows,
            },
            sort_keys=True,
        )
    )


def worker_pyarrow(args: argparse.Namespace) -> None:
    import pyarrow.compute as pc
    import pyarrow.csv as pacsv

    include_columns = list(args.columns)
    if args.filter_column is not None and args.filter_column not in include_columns:
        include_columns.append(args.filter_column)

    convert_options = pacsv.ConvertOptions(include_columns=include_columns)
    parse_options = pacsv.ParseOptions(
        delimiter=args.delimiter,
        newlines_in_values=True,
    )
    read_options = pacsv.ReadOptions(use_threads=True)
    table = pacsv.read_csv(
        str(args.csv_file),
        read_options=read_options,
        parse_options=parse_options,
        convert_options=convert_options,
    )
    if args.filter_column is not None:
        column = table[args.filter_column]
        expected = args.filter_value
        if args.case_insensitive:
            column = pc.utf8_lower(column)
            expected = expected.lower()
        table = table.filter(pc.equal(column, expected))

    arrays = {name: table[name].to_numpy(zero_copy_only=False) for name in args.columns}
    rows = len(next(iter(arrays.values()))) if arrays else 0
    array_bytes = sum(_array_nbytes(array) for array in arrays.values())
    print(
        json.dumps(
            {
                "array_bytes": array_bytes,
                "columns": len(arrays),
                "rows": rows,
            },
            sort_keys=True,
        )
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", type=Path)
    parser.add_argument(
        "--columns",
        action="append",
        default=[],
        help=(
            "selected column to materialize; repeat for multiple columns "
            f"(default: {', '.join(DEFAULT_COLUMNS)})"
        ),
    )
    parser.add_argument("--delimiter", default=",")
    parser.add_argument(
        "--filter-column",
        default=None,
        help="optional column used to filter rows before materializing NumPy arrays",
    )
    parser.add_argument(
        "--filter-value",
        default="",
        help="value compared with --filter-column when filtering",
    )
    parser.add_argument("--case-insensitive", action="store_true")
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=0.05,
        help="seconds between memory samples",
    )
    parser.add_argument("--worker", choices=("csvpy", "pyarrow"), help=argparse.SUPPRESS)
    args = parser.parse_args()

    if not args.columns:
        args.columns = list(DEFAULT_COLUMNS)
    if len(args.delimiter) != 1:
        raise SystemExit("--delimiter must be a single character")
    if args.filter_column is None and args.filter_value:
        raise SystemExit("--filter-value requires --filter-column")
    return args


def main() -> None:
    args = parse_args()

    if args.worker == "csvpy":
        worker_csvpy(args)
        return
    if args.worker == "pyarrow":
        worker_pyarrow(args)
        return

    csvpy_label = "csvpy_read_numpy"
    pyarrow_label = "pyarrow_read_csv_to_numpy"
    if args.filter_column is not None:
        csvpy_label = "csvpy_filter_to_numpy"
        pyarrow_label = "pyarrow_filter_to_numpy"

    results = [
        run_worker("csvpy", csvpy_label, args),
        run_worker("pyarrow", pyarrow_label, args),
    ]
    print_results(args.csv_file, args, results)


if __name__ == "__main__":
    main()
