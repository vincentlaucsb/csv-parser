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
        batch_size: int = 8192,
    ):
        if typed is not None:
            cast = typed
        fmt = _make_format(delimiter, quotechar, doublequote, skipinitialspace, strict)
        self._init_reader(csvfile, fmt, cast, batch_size)


def reader(csvfile, dialect="excel", **fmtparams) -> _Reader:
    if dialect != "excel":
        raise NotImplementedError("csvpy.reader currently supports only the default excel dialect")
    return _Reader(csvfile, **fmtparams)


class _Rows(_BaseReader):
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
            no_header=fieldnames is not None,
        )
        self._init_reader(csvfile, fmt, cast, batch_size)


def rows(csvfile, dialect="excel", **fmtparams) -> _Rows:
    if dialect != "excel":
        raise NotImplementedError("csvpy.rows currently supports only the default excel dialect")
    return _Rows(csvfile, **fmtparams)
