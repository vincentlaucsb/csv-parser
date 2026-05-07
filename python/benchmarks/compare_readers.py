#!/usr/bin/env python3
"""Compare csvpy.reader, stdlib csv.reader, and pandas CSV readers."""

from __future__ import annotations

import argparse
import csv
import inspect
import time
from pathlib import Path


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

    measure("csvpy_reader", path, bench_csvpy)
    measure("stdlib_csv_reader", path, bench_stdlib)
    measure("pandas_pyarrow", path, bench_pandas_pyarrow)
    measure("pandas_default", path, bench_pandas_default)


if __name__ == "__main__":
    main()
