#!/usr/bin/env python3
"""Compare selected-column CSV-to-NumPy materialization.

The benchmark normalizes both tools to a dictionary-like set of NumPy arrays for
the same selected columns. With --filter-column/--filter-value, it first filters
rows and then materializes the selected columns. The parent process launches
each tool in a fresh Python interpreter and samples wall time plus peak
resident/working-set memory. Filtered runs compare the Python row-loop path,
native predicate materialization, serial and parallel streaming native predicate
batches, pyarrow, and Polars lazy scanning.
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

import _support as bench_support


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_PACKAGE_ROOT = REPO_ROOT / "python"
BUILD_ROOTS = (REPO_ROOT / "build", REPO_ROOT / "out")
DEFAULT_COLUMNS = ("region", "price", "year", "odometer")
DEFAULT_COLUMN_COUNT = 4


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
    extra_args: tuple[str, ...] = ()


@dataclass(frozen=True)
class FilterSpec:
    column: str
    op: str
    value: str


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

    env_extension = os.environ.get("FASTPYCSV_EXTENSION_PATH")
    if env_extension:
        _load_fastpycsv_extension_from_build(Path(env_extension))
        import fastpycsv  # noqa: F401
        return

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
    for column in args.columns:
        command.extend(["--columns", column])
    if args.filter_column is not None:
        command.extend(["--filter-column", args.filter_column])
        command.extend(["--filter-value", args.filter_value])
    for spec in args.filters:
        command.extend(["--filter", f"{spec.column}{spec.op}{spec.value}"])
    if args.case_insensitive:
        command.append("--case-insensitive")
    command.extend(["--batch-size", str(args.batch_size)])
    command.extend(["--arrow-block-size", str(args.arrow_block_size)])
    if args.fastpycsv_batch_schema is not None:
        command.extend(["--fastpycsv-batch-schema", args.fastpycsv_batch_schema])
    command.extend(worker.extra_args)

    env = bench_support.benchmark_env()

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


def mib(value: int) -> float:
    return value / (1024 * 1024)


def _worker_payload(stdout: str) -> dict:
    try:
        return json.loads(stdout.splitlines()[-1])
    except (IndexError, json.JSONDecodeError):
        return {}


def _median(values: list[float]) -> float:
    return float(statistics.median(values))


def _group_results(results: list[RunResult]) -> list[tuple[str, list[RunResult]]]:
    grouped: dict[str, list[RunResult]] = {}
    order: list[str] = []
    for result in results:
        if result.name not in grouped:
            grouped[result.name] = []
            order.append(result.name)
        grouped[result.name].append(result)
    return [(name, grouped[name]) for name in order]


def active_filters(args: argparse.Namespace) -> list[FilterSpec]:
    filters = list(args.filters)
    if args.filter_column is not None:
        filters.insert(0, FilterSpec(args.filter_column, "=", args.filter_value))
    return filters


def print_results(path: Path, args: argparse.Namespace, results: list[RunResult]) -> None:
    size = path.stat().st_size
    size_mib = size / (1024 * 1024)
    print(f"file={path}")
    print(f"size={size} bytes ({size_mib:.3f} MiB)")
    print(f"columns={', '.join(args.columns)}")
    filters = active_filters(args)
    if filters:
        print("filters=" + " AND ".join(f"{spec.column!r} {spec.op} {spec.value!r}" for spec in filters))
    print(f"warmups={args.warmup_runs} measured_runs={args.runs}")
    print()
    headers = [
        "Tool",
        "Status",
        "MedianSeconds",
        "MinSeconds",
        "MaxSeconds",
        "MiB/s",
        "PeakRSSMiB",
        "Rows",
        "Columns",
        "ArrayBytes",
    ]
    rows: list[list[str]] = []
    for name, group in _group_results(results):
        ok_results = [result for result in group if result.returncode == 0]
        status = "ok" if len(ok_results) == len(group) else f"error({len(group) - len(ok_results)}/{len(group)})"
        seconds = [result.wall_seconds for result in ok_results]
        median_seconds = _median(seconds) if seconds else 0.0
        min_seconds = min(seconds) if seconds else 0.0
        max_seconds = max(seconds) if seconds else 0.0
        peak_rss = _median([float(result.peak_rss_bytes) for result in ok_results]) if ok_results else 0.0
        payload = _worker_payload(ok_results[0].stdout) if ok_results else {}
        throughput = size_mib / median_seconds if median_seconds > 0 else 0.0
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
            str(payload.get("array_bytes", "")),
        ])

    print_table(headers, rows, left_aligned={0, 1})

    for result in results:
        if result.returncode != 0:
            print()
            print(f"{result.name} stderr:")
            print(result.stderr.rstrip() or "<empty>")


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


def default_columns_for_file(path: Path, delimiter: str) -> list[str]:
    header = _header(path, delimiter)
    if all(column in header for column in DEFAULT_COLUMNS):
        return list(DEFAULT_COLUMNS)
    return header[:DEFAULT_COLUMN_COUNT]


def validate_columns(args: argparse.Namespace) -> None:
    header = _header(args.csv_file, args.delimiter)
    missing_selected = [column for column in args.columns if column not in header]
    if missing_selected:
        raise SystemExit(
            f"selected column not found: {missing_selected[0]!r}; available columns: {header}"
        )

    for spec in active_filters(args):
        if spec.column not in header:
            raise SystemExit(
                f"filter column not found: {spec.column!r}; available columns: {header}"
            )


def _parse_filter(text: str) -> FilterSpec:
    for op in ("<=", ">=", "<", ">", "="):
        column, separator, value = text.partition(op)
        if separator:
            if not column:
                raise argparse.ArgumentTypeError("--filter must include a column name")
            return FilterSpec(column, op, value)
    raise argparse.ArgumentTypeError("--filter must be in COLUMN=VALUE, COLUMN<VALUE, COLUMN<=VALUE, COLUMN>VALUE, or COLUMN>=VALUE form")


def _compare_filter_value(value: object, spec: FilterSpec, case_insensitive: bool) -> bool:
    if spec.op == "=":
        text = str(value)
        expected = spec.value
        if case_insensitive:
            text = text.lower()
            expected = expected.lower()
        return text == expected

    try:
        left = float(value)
        right = float(spec.value)
    except (TypeError, ValueError):
        return False

    if spec.op == "<":
        return left < right
    if spec.op == "<=":
        return left <= right
    if spec.op == ">":
        return left > right
    if spec.op == ">=":
        return left >= right
    raise AssertionError(f"unexpected filter operator: {spec.op}")


def _numpy_filter_mask(array: object, spec: FilterSpec, case_insensitive: bool):
    import numpy as np

    return np.asarray(
        [_compare_filter_value(value, spec, case_insensitive) for value in array],
        dtype=np.bool_,
    )


def _native_predicate(fastpycsv, spec: FilterSpec, case_insensitive: bool):
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


def worker_fastpycsv(args: argparse.Namespace) -> None:
    ensure_fastpycsv_available()
    import fastpycsv

    filters = active_filters(args)
    if not filters:
        arrays = fastpycsv.read_numpy(str(args.csv_file), columns=args.columns)
    else:
        header = _header(args.csv_file, args.delimiter)
        selected_indices = [_column_index(header, column) for column in args.columns]
        filter_indices = [_column_index(header, spec.column) for spec in filters]
        collected: dict[str, list[object]] = {column: [] for column in args.columns}

        for row in fastpycsv.reader(args.csv_file, delimiter=args.delimiter, cast=True):
            row_matches = True
            for filter_index, spec in zip(filter_indices, filters):
                if not _compare_filter_value(row[filter_index], spec, args.case_insensitive):
                    row_matches = False
                    break
            if not row_matches:
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


def worker_fastpycsv_predicate(args: argparse.Namespace, *, parallel: bool) -> None:
    filters = active_filters(args)
    if not filters:
        raise SystemExit("fastpycsv predicate worker requires --filter-column")

    ensure_fastpycsv_available()
    import fastpycsv

    previous_parallel = os.environ.get("FASTPYCSV_PREDICATE_PARALLEL")
    os.environ["FASTPYCSV_PREDICATE_PARALLEL"] = "1" if parallel else "0"
    try:
        predicates = [_native_predicate(fastpycsv, spec, args.case_insensitive) for spec in filters]
        predicate = predicates[0] if len(predicates) == 1 else fastpycsv.all_of(*predicates)
        arrays = fastpycsv.read_numpy(
            str(args.csv_file),
            columns=args.columns,
            predicate=predicate,
        )
    finally:
        if previous_parallel is None:
            os.environ.pop("FASTPYCSV_PREDICATE_PARALLEL", None)
        else:
            os.environ["FASTPYCSV_PREDICATE_PARALLEL"] = previous_parallel

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


def worker_fastpycsv_batches(args: argparse.Namespace, *, parallel: bool | None = None) -> None:
    filters = active_filters(args)

    ensure_fastpycsv_available()
    import fastpycsv

    predicate = None
    if filters:
        predicates = [_native_predicate(fastpycsv, spec, args.case_insensitive) for spec in filters]
        predicate = predicates[0] if len(predicates) == 1 else fastpycsv.all_of(*predicates)

    previous_parallel = os.environ.get("FASTPYCSV_PREDICATE_PARALLEL")
    if parallel is not None:
        os.environ["FASTPYCSV_PREDICATE_PARALLEL"] = "1" if parallel else "0"

    try:
        rows = 0
        array_bytes = 0
        columns = 0
        for batch in fastpycsv.read_numpy_batches(
            str(args.csv_file),
            columns=args.columns,
            predicate=predicate,
            batch_size=args.batch_size,
            schema=args.fastpycsv_batch_schema,
        ):
            if not batch:
                continue

            columns = len(batch)
            if batch:
                rows += len(next(iter(batch.values())))
            array_bytes += sum(_array_nbytes(array) for array in batch.values())
    finally:
        if parallel is not None:
            if previous_parallel is None:
                os.environ.pop("FASTPYCSV_PREDICATE_PARALLEL", None)
            else:
                os.environ["FASTPYCSV_PREDICATE_PARALLEL"] = previous_parallel

    print(
        json.dumps(
            {
                "array_bytes": array_bytes,
                "columns": columns,
                "rows": rows,
            },
            sort_keys=True,
        )
    )


def worker_pyarrow(args: argparse.Namespace) -> None:
    import pyarrow as pa
    import pyarrow.compute as pc
    import pyarrow.csv as pacsv

    filters = active_filters(args)
    include_columns = list(args.columns)
    for spec in filters:
        if spec.column not in include_columns:
            include_columns.append(spec.column)

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
    mask = None
    for spec in filters:
        next_mask = pyarrow_filter_mask(pa, pc, table[spec.column], spec, args.case_insensitive)
        mask = next_mask if mask is None else pc.and_(mask, next_mask)
    if mask is not None:
        table = table.filter(mask)

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


def pyarrow_filter_mask(pa, pc, column, spec: FilterSpec, case_insensitive: bool):
    expected = spec.value
    if spec.op == "=":
        column = pc.cast(column, pa.string())
        if case_insensitive:
            column = pc.utf8_lower(column)
            expected = expected.lower()
        return pc.equal(column, expected)
    if spec.op == "<":
        return pc.less(column, float(expected))
    if spec.op == "<=":
        return pc.less_equal(column, float(expected))
    if spec.op == ">":
        return pc.greater(column, float(expected))
    if spec.op == ">=":
        return pc.greater_equal(column, float(expected))
    raise AssertionError(f"unexpected filter operator: {spec.op}")


def worker_pyarrow_batches(args: argparse.Namespace) -> None:
    import pyarrow as pa
    import pyarrow.compute as pc
    import pyarrow.csv as pacsv

    filters = active_filters(args)
    include_columns = list(args.columns)
    for spec in filters:
        if spec.column not in include_columns:
            include_columns.append(spec.column)

    convert_options = pacsv.ConvertOptions(include_columns=include_columns)
    parse_options = pacsv.ParseOptions(
        delimiter=args.delimiter,
        newlines_in_values=True,
    )
    read_options = pacsv.ReadOptions(
        block_size=args.arrow_block_size,
        use_threads=True,
    )
    reader = pacsv.open_csv(
        str(args.csv_file),
        read_options=read_options,
        parse_options=parse_options,
        convert_options=convert_options,
    )

    rows = 0
    array_bytes = 0
    columns = 0
    for batch in reader:
        mask = None
        for spec in filters:
            next_mask = pyarrow_filter_mask(pa, pc, batch.column(spec.column), spec, args.case_insensitive)
            mask = next_mask if mask is None else pc.and_(mask, next_mask)
        if mask is not None:
            batch = batch.filter(mask)

        arrays = {name: batch.column(name).to_numpy(zero_copy_only=False) for name in args.columns}
        columns = len(arrays)
        if arrays:
            rows += len(next(iter(arrays.values())))
        array_bytes += sum(_array_nbytes(array) for array in arrays.values())

    print(
        json.dumps(
            {
                "array_bytes": array_bytes,
                "columns": columns,
                "rows": rows,
            },
            sort_keys=True,
        )
    )


def pyarrow_dataset_filter_expr(pa, pc, ds, spec: FilterSpec, case_insensitive: bool):
    expected = spec.value
    column = ds.field(spec.column)
    if spec.op == "=":
        column = column.cast(pa.string())
        if case_insensitive:
            column = pc.utf8_lower(column)
            expected = expected.lower()
        return column == expected
    if spec.op == "<":
        return column < float(expected)
    if spec.op == "<=":
        return column <= float(expected)
    if spec.op == ">":
        return column > float(expected)
    if spec.op == ">=":
        return column >= float(expected)
    raise AssertionError(f"unexpected filter operator: {spec.op}")


def pyarrow_dataset_column_types(pa, args: argparse.Namespace) -> dict[str, object]:
    column_types: dict[str, object] = {}
    for column in args.columns:
        if column in ("price", "year", "odometer"):
            column_types[column] = pa.float64()

    for spec in active_filters(args):
        if spec.op == "=":
            column_types.setdefault(spec.column, pa.string())
        else:
            column_types[spec.column] = pa.float64()

    return column_types


def worker_pyarrow_dataset(args: argparse.Namespace) -> None:
    import pyarrow as pa
    import pyarrow.compute as pc
    import pyarrow.csv as pacsv
    import pyarrow.dataset as ds

    filters = active_filters(args)
    parse_options = pacsv.ParseOptions(
        delimiter=args.delimiter,
        newlines_in_values=True,
    )
    read_options = pacsv.ReadOptions(use_threads=True)
    convert_options = pacsv.ConvertOptions(
        column_types=pyarrow_dataset_column_types(pa, args),
    )
    dataset = ds.dataset(
        str(args.csv_file),
        format=ds.CsvFileFormat(
            parse_options=parse_options,
            read_options=read_options,
            convert_options=convert_options,
        ),
    )

    expression = None
    for spec in filters:
        next_expression = pyarrow_dataset_filter_expr(pa, pc, ds, spec, args.case_insensitive)
        expression = next_expression if expression is None else expression & next_expression

    scanner = ds.Scanner.from_dataset(
        dataset,
        columns=args.columns,
        filter=expression,
        use_threads=True,
    )
    table = scanner.to_table()
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


def _polars_filter_expr(pl, spec: FilterSpec, case_insensitive: bool):
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


def worker_polars(args: argparse.Namespace) -> None:
    import polars as pl

    frame = pl.scan_csv(
        str(args.csv_file),
        separator=args.delimiter,
    )
    for spec in active_filters(args):
        frame = frame.filter(_polars_filter_expr(pl, spec, args.case_insensitive))

    frame = frame.select(args.columns).collect()
    arrays = {name: frame[name].to_numpy() for name in args.columns}
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


def worker_polars_batches(args: argparse.Namespace) -> None:
    import polars as pl

    frame = pl.scan_csv(
        str(args.csv_file),
        separator=args.delimiter,
    )
    for spec in active_filters(args):
        frame = frame.filter(_polars_filter_expr(pl, spec, args.case_insensitive))

    frame = frame.select(args.columns)
    rows = 0
    array_bytes = 0
    columns = 0
    for batch in frame.collect_batches(chunk_size=args.batch_size):
        arrays = {name: batch[name].to_numpy() for name in args.columns}
        columns = len(arrays)
        if arrays:
            rows += len(next(iter(arrays.values())))
        array_bytes += sum(_array_nbytes(array) for array in arrays.values())

    print(
        json.dumps(
            {
                "array_bytes": array_bytes,
                "columns": columns,
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
            f"(default: {', '.join(DEFAULT_COLUMNS)} when present, otherwise first {DEFAULT_COLUMN_COUNT} columns)"
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
    parser.add_argument(
        "--filter",
        dest="filters",
        action="append",
        default=[],
        type=_parse_filter,
        help="additional filter in COLUMN=VALUE, COLUMN<VALUE, COLUMN<=VALUE, COLUMN>VALUE, or COLUMN>=VALUE form; repeat to AND filters",
    )
    parser.add_argument("--case-insensitive", action="store_true")
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
        "--batch-size",
        type=int,
        default=50000,
        help="row count per fastpycsv and Polars streaming batch",
    )
    parser.add_argument(
        "--fastpycsv-batch-schema",
        choices=("sample", "batch", "global"),
        default="sample",
        help="schema inference mode for fastpycsv.read_numpy_batches() workers",
    )
    parser.add_argument(
        "--arrow-block-size",
        type=int,
        default=1 << 20,
        help="byte block size for pyarrow.csv.open_csv() streaming batches",
    )
    parser.add_argument(
        "--only",
        action="append",
        choices=("fastpycsv", "pyarrow", "polars"),
        help="only benchmark one library family; repeat to include more than one",
    )
    parser.add_argument(
        "--worker",
        choices=(
            "fastpycsv",
            "fastpycsv_batches",
            "fastpycsv_batches_serial",
            "fastpycsv_batches_parallel",
            "fastpycsv_predicate_serial",
            "fastpycsv_predicate_parallel",
            "pyarrow",
            "pyarrow_batches",
            "pyarrow_dataset",
            "polars",
            "polars_batches",
        ),
        help=argparse.SUPPRESS,
    )
    args = parser.parse_args()

    if not args.columns:
        args.columns = default_columns_for_file(args.csv_file, args.delimiter)
    if len(args.delimiter) != 1:
        raise SystemExit("--delimiter must be a single character")
    if args.filter_column is None and args.filter_value:
        raise SystemExit("--filter-value requires --filter-column")
    if args.runs < 1:
        raise SystemExit("--runs must be at least 1")
    if args.warmup_runs < 0:
        raise SystemExit("--warmup-runs cannot be negative")
    if args.batch_size < 1:
        raise SystemExit("--batch-size must be at least 1")
    if args.arrow_block_size < 1:
        raise SystemExit("--arrow-block-size must be at least 1")
    if not args.worker:
        validate_columns(args)
    return args


def benchmark_workers(args: argparse.Namespace) -> list[WorkerSpec]:
    fastpycsv_label = "fastpycsv_read_numpy"
    pyarrow_label = "pyarrow_read_csv_to_numpy"
    polars_label = "polars_scan_csv_to_numpy"
    if active_filters(args):
        fastpycsv_label = "fastpycsv_python_filter_to_numpy"
        pyarrow_label = "pyarrow_filter_to_numpy"
        polars_label = "polars_filter_to_numpy"

    workers = [WorkerSpec("fastpycsv", fastpycsv_label)]
    if not active_filters(args):
        workers.append(WorkerSpec("fastpycsv_batches", "fastpycsv_read_numpy_batches_sample", ("--fastpycsv-batch-schema", "sample")))
        workers.append(WorkerSpec("fastpycsv_batches", "fastpycsv_read_numpy_batches_global", ("--fastpycsv-batch-schema", "global")))
    else:
        workers.append(WorkerSpec("fastpycsv_batches_serial", "fastpycsv_serial_predicate_batches_sample_to_numpy", ("--fastpycsv-batch-schema", "sample")))
        workers.append(WorkerSpec("fastpycsv_batches_parallel", "fastpycsv_parallel_predicate_batches_sample_to_numpy", ("--fastpycsv-batch-schema", "sample")))
        workers.append(WorkerSpec("fastpycsv_batches_parallel", "fastpycsv_parallel_predicate_batches_global_to_numpy", ("--fastpycsv-batch-schema", "global")))
    if active_filters(args):
        workers.append(WorkerSpec("fastpycsv_predicate_serial", "fastpycsv_serial_predicate_to_numpy"))
        workers.append(WorkerSpec("fastpycsv_predicate_parallel", "fastpycsv_parallel_predicate_to_numpy"))
    workers.append(WorkerSpec("pyarrow", pyarrow_label))
    if active_filters(args):
        workers.append(WorkerSpec("pyarrow_dataset", "pyarrow_dataset_filter_to_numpy"))
    workers.append(WorkerSpec("pyarrow_batches", "pyarrow_streaming_batches_to_numpy"))
    workers.append(WorkerSpec("polars", polars_label))
    workers.append(WorkerSpec("polars_batches", "polars_streaming_batches_to_numpy"))
    return workers


def rotated_workers(workers: list[WorkerSpec], offset: int) -> list[WorkerSpec]:
    if not workers:
        return []
    start = offset % len(workers)
    return workers[start:] + workers[:start]


def worker_libraries(worker: WorkerSpec) -> tuple[str, ...]:
    if worker.worker.startswith("fastpycsv"):
        return ("fastpycsv",)
    if worker.worker.startswith("pyarrow"):
        return ("pyarrow",)
    if worker.worker.startswith("polars"):
        return ("polars",)
    return ()


def selected_workers(workers: list[WorkerSpec], only: list[str] | None) -> list[WorkerSpec]:
    if not only:
        return workers

    selected = [
        worker
        for worker in workers
        if any(library in worker_libraries(worker) for library in only)
    ]
    if not selected:
        choices = sorted({library for worker in workers for library in worker_libraries(worker)})
        raise SystemExit(f"--only selected no benchmark workers; available libraries: {', '.join(choices)}")
    return selected


def main() -> None:
    args = parse_args()

    if args.worker == "fastpycsv":
        worker_fastpycsv(args)
        return
    if args.worker == "fastpycsv_batches":
        worker_fastpycsv_batches(args)
        return
    if args.worker == "fastpycsv_batches_serial":
        worker_fastpycsv_batches(args, parallel=False)
        return
    if args.worker == "fastpycsv_batches_parallel":
        worker_fastpycsv_batches(args, parallel=True)
        return
    if args.worker == "fastpycsv_predicate_serial":
        worker_fastpycsv_predicate(args, parallel=False)
        return
    if args.worker == "fastpycsv_predicate_parallel":
        worker_fastpycsv_predicate(args, parallel=True)
        return
    if args.worker == "pyarrow":
        worker_pyarrow(args)
        return
    if args.worker == "pyarrow_batches":
        worker_pyarrow_batches(args)
        return
    if args.worker == "pyarrow_dataset":
        worker_pyarrow_dataset(args)
        return
    if args.worker == "polars":
        worker_polars(args)
        return
    if args.worker == "polars_batches":
        worker_polars_batches(args)
        return

    workers = selected_workers(benchmark_workers(args), args.only)
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
