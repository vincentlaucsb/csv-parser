"""Shared helpers for Python benchmark scripts."""

from __future__ import annotations

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
import argparse
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_PACKAGE_ROOT = REPO_ROOT / "python"
BUILD_ROOTS = (REPO_ROOT / "build", REPO_ROOT / "out")
_BUILT_FASTPYCSV_EXTENSION: Path | None = None


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
    libraries: tuple[str, ...] = ()
    extra_args: tuple[str, ...] = ()


@dataclass(frozen=True)
class FilterSpec:
    column: str
    op: str
    value: str


def extension_suffixes() -> tuple[str, ...]:
    return tuple(importlib.machinery.EXTENSION_SUFFIXES)


def is_compatible_extension_name(name: str) -> bool:
    for suffix in extension_suffixes():
        if not name.endswith(suffix):
            continue

        # Windows exposes ".pyd" as a generic fallback suffix. Reject tagged
        # extensions for other CPython ABIs while still allowing untagged pyds.
        if suffix == ".pyd" and ".cp" in name:
            continue

        return True

    return False


def find_built_fastpycsv_extension() -> Path | None:
    global _BUILT_FASTPYCSV_EXTENSION
    if _BUILT_FASTPYCSV_EXTENSION is not None:
        return _BUILT_FASTPYCSV_EXTENSION

    candidates: list[Path] = []
    for build_root in BUILD_ROOTS:
        if not build_root.exists():
            continue

        for candidate in build_root.rglob("fastpycsv*"):
            if (
                candidate.is_file()
                and candidate.name.startswith("fastpycsv")
                and is_compatible_extension_name(candidate.name)
            ):
                candidates.append(candidate)

    if not candidates:
        return None

    _BUILT_FASTPYCSV_EXTENSION = max(candidates, key=lambda path: path.stat().st_mtime)
    return _BUILT_FASTPYCSV_EXTENSION


def load_fastpycsv_extension_from_build(extension_path: Path) -> None:
    spec = importlib.util.spec_from_file_location("fastpycsv.fastpycsv", extension_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load fastpycsv extension from {extension_path}")

    module = importlib.util.module_from_spec(spec)
    sys.modules["fastpycsv.fastpycsv"] = module
    spec.loader.exec_module(module)


def ensure_fastpycsv_available() -> None:
    if str(PYTHON_PACKAGE_ROOT) not in sys.path:
        sys.path.insert(0, str(PYTHON_PACKAGE_ROOT))

    env_extension = os.environ.get("FASTPYCSV_EXTENSION_PATH")
    extension_path = Path(env_extension) if env_extension else find_built_fastpycsv_extension()
    if extension_path is not None:
        try:
            load_fastpycsv_extension_from_build(extension_path)
            import fastpycsv  # noqa: F401
            return
        except ImportError as exc:
            raise SystemExit(
                f"found fastpycsv at {extension_path}, but it could not be imported: {exc}"
            ) from exc

    try:
        import fastpycsv  # noqa: F401
        return
    except ImportError as exc:
        suffixes = ", ".join(extension_suffixes())
        raise SystemExit(
            "fastpycsv is not built for this Python interpreter. "
            f"Expected a fastpycsv extension under {BUILD_ROOTS[0]} or {BUILD_ROOTS[1]} "
            f"with suffix: {suffixes}"
        ) from exc


def benchmark_env() -> dict[str, str]:
    env = os.environ.copy()
    env["PYTHONPATH"] = (
        str(PYTHON_PACKAGE_ROOT)
        if not env.get("PYTHONPATH")
        else str(PYTHON_PACKAGE_ROOT) + os.pathsep + env["PYTHONPATH"]
    )
    if not env.get("FASTPYCSV_EXTENSION_PATH"):
        extension_path = find_built_fastpycsv_extension()
        if extension_path is not None:
            env["FASTPYCSV_EXTENSION_PATH"] = str(extension_path)
    return env


def windows_memory_bytes(pid: int) -> int:
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

    process_query_limited_information = 0x1000
    process_vm_read = 0x0010
    handle = ctypes.windll.kernel32.OpenProcess(
        process_query_limited_information | process_vm_read,
        False,
        pid,
    )
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


def linux_memory_bytes(pid: int) -> int:
    try:
        for line in Path(f"/proc/{pid}/status").read_text(encoding="utf-8").splitlines():
            if line.startswith("VmHWM:"):
                return int(line.split()[1]) * 1024
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024
    except OSError:
        return 0
    return 0


def ps_memory_bytes(pid: int) -> int:
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
        return windows_memory_bytes(pid)
    if system == "Linux":
        return linux_memory_bytes(pid)
    return ps_memory_bytes(pid)


def run_process(
    label: str,
    command: list[str],
    poll_interval: float,
    timeout_seconds: float | None = None,
) -> RunResult:
    env = benchmark_env()
    start = time.perf_counter()
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )

    peak = 0
    timed_out = False
    while process.poll() is None:
        peak = max(peak, process_memory_bytes(process.pid))
        if timeout_seconds is not None and (time.perf_counter() - start) >= timeout_seconds:
            timed_out = True
            process.kill()
            break
        time.sleep(poll_interval)

    peak = max(peak, process_memory_bytes(process.pid))
    stdout, stderr = process.communicate()
    wall = time.perf_counter() - start
    if timed_out:
        stderr = (
            stderr
            + ("\n" if stderr and not stderr.endswith("\n") else "")
            + f"benchmark worker timed out after {timeout_seconds:.3f} seconds"
        )
    return RunResult(label, process.returncode, wall, peak, stdout, stderr)


def selected_workers(workers: list[WorkerSpec], only: list[str] | None) -> list[WorkerSpec]:
    if not only:
        return workers

    selected = [
        worker
        for worker in workers
        if any(library in worker.libraries for library in only)
    ]
    if not selected:
        choices = sorted({library for worker in workers for library in worker.libraries})
        raise SystemExit(f"--only selected no benchmark workers; available libraries: {', '.join(choices)}")
    return selected


def worker_payload(stdout: str) -> dict:
    try:
        return json.loads(stdout.splitlines()[-1])
    except (IndexError, json.JSONDecodeError):
        return {}


def group_results(results: list[RunResult]) -> list[tuple[str, list[RunResult]]]:
    grouped: dict[str, list[RunResult]] = {}
    order: list[str] = []
    for result in results:
        if result.name not in grouped:
            grouped[result.name] = []
            order.append(result.name)
        grouped[result.name].append(result)
    return [(name, grouped[name]) for name in order]


def median(values: list[float]) -> float:
    return float(statistics.median(values)) if values else 0.0


def mib(value: int | float) -> float:
    return float(value) / (1024 * 1024)


def print_table(headers: list[str], rows: list[list[str]], left_aligned: set[int]) -> None:
    widths = [
        max(len(headers[index]), *(len(row[index]) for row in rows))
        for index in range(len(headers))
    ]

    def format_cell(index: int, value: str) -> str:
        if index in left_aligned:
            return value.ljust(widths[index])
        return value.rjust(widths[index])

    print("  ".join(format_cell(index, value) for index, value in enumerate(headers)))
    print("  ".join("-" * width for width in widths))
    for row in rows:
        print("  ".join(format_cell(index, value) for index, value in enumerate(row)))


def rotated_workers(workers: list[WorkerSpec], offset: int) -> list[WorkerSpec]:
    if not workers:
        return []
    start = offset % len(workers)
    return workers[start:] + workers[:start]


def parse_filter(text: str) -> FilterSpec:
    for op in ("<=", ">=", "<", ">", "="):
        column, separator, value = text.partition(op)
        if separator:
            if not column:
                raise argparse.ArgumentTypeError("--filter must include a column name")
            return FilterSpec(column, op, value)
    raise argparse.ArgumentTypeError(
        "--filter must be in COLUMN=VALUE, COLUMN<VALUE, COLUMN<=VALUE, COLUMN>VALUE, or COLUMN>=VALUE form"
    )


def active_filters(
    args: argparse.Namespace,
    *,
    default_column: str | None = None,
    default_value: str = "",
) -> list[FilterSpec]:
    filters = list(getattr(args, "filters", []))
    column = getattr(args, "column", None)
    if column is not None:
        filters.insert(0, FilterSpec(column, "=", getattr(args, "value", "")))
    elif not filters and default_column is not None:
        filters.append(FilterSpec(default_column, "=", default_value))
    return filters


def filter_description(filters: list[FilterSpec]) -> str:
    return " AND ".join(f"{spec.column!r} {spec.op} {spec.value!r}" for spec in filters)


def native_predicate(fastpycsv, spec: FilterSpec, case_insensitive: bool):
    if spec.op == "=":
        return fastpycsv.equal(spec.column, spec.value, case_sensitive=not case_insensitive)
    if spec.op == "<":
        return fastpycsv.less(spec.column, spec.value)
    if spec.op == "<=":
        return fastpycsv.less_equal(spec.column, spec.value)
    if spec.op == ">":
        return fastpycsv.greater(spec.column, spec.value)
    if spec.op == ">=":
        return fastpycsv.greater_equal(spec.column, spec.value)
    raise AssertionError(f"unexpected filter operator: {spec.op}")


def pyarrow_filter_mask(pa, pc, column, spec: FilterSpec, case_insensitive: bool):
    expected = spec.value
    if spec.op == "=":
        column = pc.cast(column, pa.string())
        if case_insensitive:
            column = pc.utf8_lower(column)
            expected = expected.lower()
        return pc.equal(column, expected)

    column = pc.cast(column, pa.float64(), safe=False)
    expected_float = float(expected)
    if spec.op == "<":
        return pc.less(column, expected_float)
    if spec.op == "<=":
        return pc.less_equal(column, expected_float)
    if spec.op == ">":
        return pc.greater(column, expected_float)
    if spec.op == ">=":
        return pc.greater_equal(column, expected_float)
    raise AssertionError(f"unexpected filter operator: {spec.op}")


def polars_filter_expr(pl, spec: FilterSpec, case_insensitive: bool):
    if spec.op == "=":
        column = pl.col(spec.column).cast(pl.Utf8)
        expected = spec.value
        if case_insensitive:
            column = column.str.to_lowercase()
            expected = expected.lower()
        return column == expected

    column = pl.col(spec.column).cast(pl.Float64, strict=False)
    expected = float(spec.value)
    if spec.op == "<":
        return column < expected
    if spec.op == "<=":
        return column <= expected
    if spec.op == ">":
        return column > expected
    if spec.op == ">=":
        return column >= expected
    raise AssertionError(f"unexpected filter operator: {spec.op}")


def unique_filter_columns(filters: list[FilterSpec]) -> list[str]:
    columns: list[str] = []
    for spec in filters:
        if spec.column not in columns:
            columns.append(spec.column)
    return columns
