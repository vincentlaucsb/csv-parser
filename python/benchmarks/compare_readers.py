#!/usr/bin/env python3
"""Compare lazy Python row readers that produce string rows."""

from __future__ import annotations

import argparse
import csv
import time
from pathlib import Path

from _support import ensure_fastpycsv_available


def measure(name, path, func):
    size = path.stat().st_size
    start = time.perf_counter()
    try:
        rows, cols = func(path)
    except Exception as exc:
        print(f"{name}\tskip={type(exc).__name__}: {exc}")
        return

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
    for row in fastpycsv.reader(path):
        rows += 1
        cols = max(cols, len(row))
    return rows, cols


def bench_fastpycsv_reader_cast(path):
    import fastpycsv

    rows = 0
    cols = 0
    for row in fastpycsv.reader(path, cast=True):
        rows += 1
        cols = max(cols, len(row))
    return rows, cols


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
    parser.add_argument(
        "--only",
        action="append",
        choices=("stdlib", "fastpycsv"),
        help="only benchmark one library family; repeat to include more than one",
    )
    args = parser.parse_args()
    path = args.csv_file

    def selected(*libraries):
        return not args.only or any(library in libraries for library in args.only)

    if selected("fastpycsv"):
        ensure_fastpycsv_available()

    if selected("stdlib"):
        measure("stdlib_csv_reader_strings", path, bench_stdlib_strings)
        measure("stdlib_dict_reader_strings", path, bench_stdlib_dict_strings)
    if selected("fastpycsv"):
        measure("fastpycsv_reader_strings", path, bench_fastpycsv_reader_strings)
        measure("fastpycsv_reader_cast", path, bench_fastpycsv_reader_cast)


if __name__ == "__main__":
    main()
