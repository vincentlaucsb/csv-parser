#!/usr/bin/env python3
"""Compare fastpycsv lazy row reading against stdlib csv readers."""

from __future__ import annotations

import argparse
import csv
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
        # interpreter. Keep untagged fastpycsv.pyd valid while rejecting mismatches.
        if suffix == ".pyd" and ".cp" in name:
            continue

        return True

    return False


def _find_built_fastpycsv_extension():
    candidates = []
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


def _load_fastpycsv_extension_from_build(extension_path):
    spec = importlib.util.spec_from_file_location("fastpycsv.fastpycsv", extension_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load fastpycsv extension from {extension_path}")

    module = importlib.util.module_from_spec(spec)
    sys.modules["fastpycsv.fastpycsv"] = module
    spec.loader.exec_module(module)


def ensure_fastpycsv_available():
    if str(PYTHON_PACKAGE_ROOT) not in sys.path:
        sys.path.insert(0, str(PYTHON_PACKAGE_ROOT))

    try:
        import fastpycsv  # noqa: F401
        return
    except ImportError:
        sys.modules.pop("fastpycsv", None)
        sys.modules.pop("fastpycsv.fastpycsv", None)
        pass

    extension_path = _find_built_fastpycsv_extension()
    if extension_path is None:
        suffixes = ", ".join(_extension_suffixes())
        raise SystemExit(
            "fastpycsv is not built for this Python interpreter. "
            f"Expected a fastpycsv extension under {BUILD_ROOTS[0]} or {BUILD_ROOTS[1]} "
            f"with suffix: {suffixes}"
        )

    try:
        _load_fastpycsv_extension_from_build(extension_path)
        import fastpycsv  # noqa: F401
    except ImportError as exc:
        raise SystemExit(
            f"found fastpycsv at {extension_path}, but it could not be imported: {exc}"
        ) from exc


def measure(name, path, func):
    size = path.stat().st_size
    start = time.perf_counter()
    try:
        result = func(path)
    except Exception as exc:
        print(f"{name}\tskip={type(exc).__name__}: {exc}")
        return
    if result is None:
        return
    rows, cols = result
    elapsed = time.perf_counter() - start
    mib = size / (1024 * 1024)
    print(
        f"{name}\tfile={path}\tsize={size}\trows={rows}\tcolumns={cols}"
        f"\telapsed={elapsed:.6f}\tMiB/s={mib / elapsed:.3f}\trows/s={rows / elapsed:.3f}"
    )


def bench_fastpycsv_reader_strings(path):
    import fastpycsv

    rows = 0
    cols = 0
    with path.open(newline="", encoding="utf-8") as handle:
        for row in fastpycsv.reader(handle):
            rows += 1
            cols = max(cols, len(row))
    return rows, cols


def bench_fastpycsv_reader_cast(path):
    import fastpycsv

    rows = 0
    cols = 0
    with path.open(newline="", encoding="utf-8") as handle:
        for row in fastpycsv.reader(handle, cast=True):
            rows += 1
            cols = max(cols, len(row))
    return rows, cols


def bench_fastpycsv_read_numpy_dataframe(path):
    import fastpycsv
    import pandas as pd

    frame = pd.DataFrame(fastpycsv.read_numpy(str(path)))
    return len(frame), len(frame.columns)


def bench_fastpycsv_read_numpy_arrays(path):
    import fastpycsv

    arrays = fastpycsv.read_numpy(str(path))
    rows = len(next(iter(arrays.values()))) if arrays else 0
    return rows, len(arrays)


def bench_pandas_pyarrow(path):
    import pandas as pd

    frame = pd.read_csv(path, engine="pyarrow")
    return len(frame), len(frame.columns)


def bench_stdlib_strings(path):
    rows = 0
    cols = 0
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.reader(handle):
            rows += 1
            cols = max(cols, len(row))
    return rows, cols


def bench_stdlib_dict_strings(path):
    rows = 0
    cols = 0
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            rows += 1
            cols = max(cols, len(row))
    return rows, cols


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", type=Path)
    args = parser.parse_args()
    path = args.csv_file
    ensure_fastpycsv_available()

    measure("stdlib_csv_reader_strings", path, bench_stdlib_strings)
    measure("fastpycsv_reader_strings", path, bench_fastpycsv_reader_strings)
    measure("fastpycsv_reader_cast", path, bench_fastpycsv_reader_cast)
    measure("stdlib_dict_reader_strings", path, bench_stdlib_dict_strings)
    measure("fastpycsv_read_numpy_arrays", path, bench_fastpycsv_read_numpy_arrays)
    measure("fastpycsv_read_numpy_dataframe", path, bench_fastpycsv_read_numpy_dataframe)
    measure("pandas_read_csv_pyarrow", path, bench_pandas_pyarrow)


if __name__ == "__main__":
    main()
