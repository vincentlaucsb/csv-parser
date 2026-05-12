"""Lazy stdlib-adjacent reader facade over the native csvpy extension."""

from __future__ import annotations

import os
import tempfile
from typing import Iterable, Optional, Sequence

from ._format import _make_format
from .csvpy import _RowsReader


def _is_path(value: object) -> bool:
    return isinstance(value, (str, bytes, os.PathLike))


def _at_start(value: object) -> bool:
    try:
        return value.tell() == 0
    except (AttributeError, OSError):
        return False


class _TempCSV:
    def __init__(self, rows: Iterable[str]):
        handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="", delete=False)
        self.name = handle.name
        with handle:
            for row in rows:
                handle.write(row)

    def cleanup(self) -> None:
        try:
            os.unlink(self.name)
        except FileNotFoundError:
            pass


class _CSVSource:
    def __init__(self, csvfile):
        self._temp = None
        self.name = self._filename(csvfile)

    def cleanup(self) -> None:
        if self._temp is not None:
            self._temp.cleanup()

    def _filename(self, csvfile) -> str:
        if _is_path(csvfile):
            return os.fspath(csvfile)
        if (
            hasattr(csvfile, "name")
            and _is_path(csvfile.name)
            and not getattr(csvfile, "closed", False)
            and _at_start(csvfile)
        ):
            return os.fspath(csvfile.name)

        self._temp = _TempCSV(csvfile)
        return self._temp.name


class _MaterializedRows:
    def __init__(self, parent: "_BaseReader", iterator):
        self._parent = parent
        self._iterator = iterator

    def __iter__(self):
        return self

    def __next__(self):
        try:
            return next(self._iterator)
        except StopIteration:
            self._parent._source.cleanup()
            raise

    def all(self):
        try:
            return self._iterator.all()
        finally:
            self._parent._source.cleanup()

    def chunks(self, size: int):
        if size <= 0:
            raise ValueError("chunk size must be greater than zero")

        def chunk_iterator():
            while True:
                batch = self._iterator.read_chunk(size)
                if not batch:
                    self._parent._source.cleanup()
                    return
                yield batch

        return chunk_iterator()


class _BaseReader:
    def _init_reader(self, csvfile, fmt, cast: bool, batch_size: int) -> None:
        self._source = _CSVSource(csvfile)
        self._iterator = iter(_RowsReader(self._source.name, fmt, cast, batch_size))

    @property
    def fieldnames(self):
        return list(self._iterator.fieldnames)

    def get_col_names(self):
        return self.fieldnames

    def __iter__(self):
        return self

    def __next__(self):
        try:
            row = next(self._iterator)
        except StopIteration:
            self._source.cleanup()
            raise
        return row

    def filter(self, predicate, *, append: bool = True):
        self._iterator.filter(predicate, append)
        return self

    def lists(self, columns=None):
        return _MaterializedRows(self, self._iterator.lists(columns))

    def tuples(self, columns=None):
        return _MaterializedRows(self, self._iterator.tuples(columns))

    def dicts(self, columns=None):
        return _MaterializedRows(self, self._iterator.dicts(columns))

    def to_lists(self, columns=None):
        return self.lists(columns).all()

    def to_tuples(self, columns=None):
        return self.tuples(columns).all()

    def to_dicts(self, columns=None):
        return self.dicts(columns).all()

    def __del__(self):
        source = getattr(self, "_source", None)
        if source is not None:
            source.cleanup()


class _Reader(_BaseReader):
    def __init__(
        self,
        csvfile,
        *,
        delimiter: str = ",",
        quotechar: Optional[str] = '"',
        doublequote: bool = True,
        skipinitialspace: bool = False,
        strict: bool = False,
        cast: bool = False,
        typed: Optional[bool] = None,
        consume_header: bool = True,
        fieldnames: Optional[Sequence[str]] = None,
        batch_size: int = 8192,
    ):
        if typed is not None:
            cast = typed
        fmt = _make_format(
            delimiter,
            quotechar,
            doublequote,
            skipinitialspace,
            strict,
            fieldnames=fieldnames,
            no_header=(not consume_header) or fieldnames is not None,
        )
        self._init_reader(csvfile, fmt, cast, batch_size)


def reader(csvfile, dialect="excel", **fmtparams) -> _Reader:
    if dialect != "excel":
        raise NotImplementedError("csvpy.reader currently supports only the default excel dialect")
    return _Reader(csvfile, **fmtparams)
