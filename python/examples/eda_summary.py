#!/usr/bin/env python3
"""Stream basic exploratory summaries with csvpy.

Reports per-column row counts, null counts, numeric mean/stdev, and the top
observed values. This intentionally stays small and dependency-free so it can be
used as a quick smoke test for csvpy's lazy row API.
"""

from __future__ import annotations

import argparse
import csv
import importlib.machinery
import importlib.util
import math
import sys
from collections import Counter
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_PACKAGE_ROOT = REPO_ROOT / "python"
BUILD_ROOTS = (REPO_ROOT / "build", REPO_ROOT / "out")


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


class NumericStats:
    def __init__(self) -> None:
        self.count = 0
        self.mean = 0.0
        self.m2 = 0.0

    def add(self, value: float) -> None:
        self.count += 1
        delta = value - self.mean
        self.mean += delta / self.count
        self.m2 += delta * (value - self.mean)

    @property
    def stdev(self) -> float:
        if self.count < 2:
            return 0.0
        return math.sqrt(self.m2 / (self.count - 1))


class ExactValueCounts:
    approximate = False

    def __init__(self, _capacity: int) -> None:
        self.counts: Counter[str] = Counter()

    def add(self, value: str) -> None:
        self.counts[value] += 1

    def most_common(self, n: int) -> list[tuple[str, int]]:
        return self.counts.most_common(n)


class BoundedValueCounts:
    approximate = True

    def __init__(self, capacity: int) -> None:
        self.capacity = capacity
        self.counts: dict[str, int] = {}

    def add(self, value: str) -> None:
        if value in self.counts:
            self.counts[value] += 1
            return

        if len(self.counts) < self.capacity:
            self.counts[value] = 1
            return

        # Misra-Gries heavy-hitter sketch: bounded memory, approximate counts.
        # This favors frequent values while letting one-off high-cardinality
        # values disappear instead of growing RAM with the dataset.
        to_delete = []
        for key in list(self.counts):
            next_count = self.counts[key] - 1
            if next_count == 0:
                to_delete.append(key)
            else:
                self.counts[key] = next_count

        for key in to_delete:
            del self.counts[key]

    def most_common(self, n: int) -> list[tuple[str, int]]:
        return sorted(self.counts.items(), key=lambda item: item[1], reverse=True)[:n]


class ColumnSummary:
    def __init__(self, name: str, value_counts) -> None:
        self.name = name
        self.total = 0
        self.nulls = 0
        self.numeric = NumericStats()
        self.values = value_counts

    def add(self, value: Any) -> None:
        self.total += 1
        if value is None:
            self.nulls += 1
            self.values.add("<NULL>")
            return

        if isinstance(value, bool):
            self.values.add(str(value))
            return

        if isinstance(value, (int, float)):
            self.numeric.add(float(value))

        self.values.add(str(value))


def _display(value: str) -> str:
    encoding = sys.stdout.encoding or "utf-8"
    return value.encode(encoding, errors="backslashreplace").decode(encoding)


def _header_names(path: Path, delimiter: str) -> list[str]:
    with path.open(newline="", encoding="utf-8") as handle:
        try:
            return next(csv.reader(handle, delimiter=delimiter))
        except StopIteration:
            return []


def _column_names(width: int, header_names: list[str] | None) -> list[str]:
    if header_names:
        names = list(header_names)
        if width > len(names):
            names.extend(f"column_{i}" for i in range(len(names), width))
        return names[:width]

    return [f"column_{i}" for i in range(width)]


def analyze(
    path: Path,
    top_n: int,
    header: bool,
    delimiter: str,
    exact_values: bool,
    top_capacity: int,
) -> list[ColumnSummary]:
    import csvpy

    value_counts_type = ExactValueCounts if exact_values else BoundedValueCounts
    summaries: list[ColumnSummary] = []
    header_names = _header_names(path, delimiter) if header else None
    row_iter = (
        csvpy.rows(path, cast=True, delimiter=delimiter)
        if header
        else csvpy.reader(path, cast=True, delimiter=delimiter)
    )

    for row in row_iter:
        if not summaries:
            summaries = [
                ColumnSummary(name, value_counts_type(top_capacity))
                for name in _column_names(len(row), header_names)
            ]

        if len(row) > len(summaries):
            summaries.extend(
                ColumnSummary(f"column_{i}", value_counts_type(top_capacity))
                for i in range(len(summaries), len(row))
            )

        for index in range(len(row)):
            summaries[index].add(row[index])

    return summaries


def print_summary(path: Path, summaries: list[ColumnSummary], top_n: int) -> None:
    print(f"file={_display(str(path))}")
    print(f"columns={len(summaries)}")
    print()

    for summary in summaries:
        print(_display(summary.name))
        print(f"  rows: {summary.total}")
        print(f"  nulls: {summary.nulls}")
        if summary.numeric.count:
            print(f"  numeric_count: {summary.numeric.count}")
            print(f"  mean: {summary.numeric.mean:.12g}")
            print(f"  stdev: {summary.numeric.stdev:.12g}")
        else:
            print("  numeric_count: 0")
            print("  mean: n/a")
            print("  stdev: n/a")

        suffix = "approx" if summary.values.approximate else "exact"
        print(f"  top_{top_n}_{suffix}:")
        for value, count in summary.values.most_common(top_n):
            print(f"    {count}\t{_display(value)}")
        print()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", type=Path)
    parser.add_argument("-n", "--top-n", type=int, default=10)
    parser.add_argument(
        "--top-capacity",
        type=int,
        default=1024,
        help="maximum distinct values retained per column for approximate top-N",
    )
    parser.add_argument(
        "--exact-values",
        action="store_true",
        help="keep exact value counts; can use large amounts of memory on high-cardinality data",
    )
    parser.add_argument("--delimiter", default=",")
    parser.add_argument("--no-header", action="store_true")
    args = parser.parse_args()

    if args.top_n < 1:
        raise SystemExit("--top-n must be at least 1")
    if args.top_capacity < args.top_n:
        raise SystemExit("--top-capacity must be greater than or equal to --top-n")
    if len(args.delimiter) != 1:
        raise SystemExit("--delimiter must be a single character")

    ensure_csvpy_available()
    summaries = analyze(
        args.csv_file,
        args.top_n,
        not args.no_header,
        args.delimiter,
        args.exact_values,
        args.top_capacity,
    )
    print_summary(args.csv_file, summaries, args.top_n)


if __name__ == "__main__":
    main()
