#!/usr/bin/env python3
"""Compare csvpy.reader, stdlib csv.reader, and pandas CSV readers."""

from __future__ import annotations

import argparse
import csv
import inspect
import importlib.machinery
import importlib.util
import sys
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_PACKAGE_ROOT = REPO_ROOT / "python"
BUILD_ROOTS = (REPO_ROOT / "build", REPO_ROOT / "out")


def _extension_suffixes():
    return tuple(importlib.machinery.EXTENSION_SUFFIXES)


def _is_compatible_extension_name(name):
    for suffix in _extension_suffixes():
        if not name.endswith(suffix):
            continue

        # Windows includes ".pyd" as a fallback suffix, but a file tagged for a
        # different CPython ABI (for example cp310) will not load into this
        # interpreter. Keep untagged csvpy.pyd valid while rejecting mismatches.
        if suffix == ".pyd" and ".cp" in name:
            continue

        return True

    return False


def _find_built_csvpy_extension():
    candidates = []
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


def _load_csvpy_extension_from_build(extension_path):
    spec = importlib.util.spec_from_file_location("csvpy.csvpy", extension_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load csvpy extension from {extension_path}")

    module = importlib.util.module_from_spec(spec)
    sys.modules["csvpy.csvpy"] = module
    spec.loader.exec_module(module)


def ensure_csvpy_available():
    if str(PYTHON_PACKAGE_ROOT) not in sys.path:
        sys.path.insert(0, str(PYTHON_PACKAGE_ROOT))

    try:
        import csvpy  # noqa: F401
        return
    except ImportError:
        sys.modules.pop("csvpy", None)
        sys.modules.pop("csvpy.csvpy", None)
        pass

    extension_path = _find_built_csvpy_extension()
    if extension_path is None:
        suffixes = ", ".join(_extension_suffixes())
        raise SystemExit(
            "csvpy is not built for this Python interpreter. "
            f"Expected a csvpy extension under {BUILD_ROOTS[0]} or {BUILD_ROOTS[1]} "
            f"with suffix: {suffixes}"
        )

    try:
        _load_csvpy_extension_from_build(extension_path)
        import csvpy  # noqa: F401
    except ImportError as exc:
        raise SystemExit(
            f"found csvpy at {extension_path}, but it could not be imported: {exc}"
        ) from exc


def measure(name, path, func):
    size = path.stat().st_size
    start = time.perf_counter()
    result = func(path)
    if result is None:
        return
    rows, cols = result
    elapsed = time.perf_counter() - start
    mib = size / (1024 * 1024)
    print(
        f"{name}\tfile={path}\tsize={size}\trows={rows}\tcolumns={cols}"
        f"\telapsed={elapsed:.6f}\tMiB/s={mib / elapsed:.3f}\trows/s={rows / elapsed:.3f}"
    )


def bench_csvpy(path):
    import csvpy

    rows = 0
    cols = 0
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csvpy.reader(handle):
            rows += 1
            cols = max(cols, len(row))
    return rows, cols


def bench_stdlib(path):
    rows = 0
    cols = 0
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.reader(handle):
            rows += 1
            cols = max(cols, len(row))
    return rows, cols


def bench_pandas_pyarrow(path):
    try:
        import pandas as pd
        import pyarrow  # noqa: F401
    except ImportError as exc:
        print(f"pandas_pyarrow\tskip={exc}")
        return

    kwargs = {"engine": "pyarrow", "header": None}
    if "dtype_backend" in inspect.signature(pd.read_csv).parameters:
        kwargs["dtype_backend"] = "pyarrow"
    frame = pd.read_csv(path, **kwargs)
    return len(frame.index), len(frame.columns)


def bench_pandas_default(path):
    try:
        import pandas as pd
    except ImportError as exc:
        print(f"pandas_default\tskip={exc}")
        return

    frame = pd.read_csv(path, header=None)
    return len(frame.index), len(frame.columns)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", type=Path)
    args = parser.parse_args()
    path = args.csv_file
    ensure_csvpy_available()

    measure("csvpy_reader", path, bench_csvpy)
    measure("stdlib_csv_reader", path, bench_stdlib)
    measure("pandas_pyarrow", path, bench_pandas_pyarrow)
    measure("pandas_default", path, bench_pandas_default)


if __name__ == "__main__":
    main()
