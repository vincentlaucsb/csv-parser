"""Stdlib-like reader facade for the csvpy pybind11 module."""

from __future__ import annotations

import os
import tempfile
from pathlib import Path
from typing import Iterable, Iterator, Optional, Sequence

from .csvpy import DataType, Field, Format, Reader, VariableColumnPolicy


def _format(
    delimiter: str = ",",
    quotechar: Optional[str] = '"',
    doublequote: bool = True,
    skipinitialspace: bool = False,
    strict: bool = False,
    fieldnames: Optional[Sequence[str]] = None,
) -> Format:
    if len(delimiter) != 1:
        raise TypeError("delimiter must be a one-character string")
    if quotechar is not None and len(quotechar) != 1:
        raise TypeError("quotechar must be a one-character string or None")
    if not doublequote:
        raise NotImplementedError("csvpy.reader does not support doublequote=False")

    fmt = Format().delimiter(delimiter).no_header()
    if quotechar is None:
        fmt.quote(False)
    else:
        fmt.quote(quotechar)
    if skipinitialspace:
        fmt.trim([" "])
    if strict:
        fmt.variable_columns(VariableColumnPolicy.THROW)
    if fieldnames is not None:
        fmt.column_names(list(fieldnames))
    return fmt


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


class _Reader:
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
    ):
        if typed is not None:
            cast = typed
        self._cast = cast
        self._temp = None
        fmt = _format(delimiter, quotechar, doublequote, skipinitialspace, strict)
        if _is_path(csvfile):
            filename = os.fspath(csvfile)
        elif (
            hasattr(csvfile, "name")
            and _is_path(csvfile.name)
            and not getattr(csvfile, "closed", False)
            and _at_start(csvfile)
        ):
            filename = os.fspath(csvfile.name)
        else:
            self._temp = _TempCSV(csvfile)
            filename = self._temp.name
        self._iterator = iter(Reader(filename, fmt))

    def __iter__(self) -> "_Reader":
        return self

    def __next__(self) -> list:
        try:
            row = next(self._iterator)
        except StopIteration:
            if self._temp is not None:
                self._temp.cleanup()
            raise
        return [_field_value(row[i], self._cast) for i in range(row.size())]

    def __del__(self):
        if self._temp is not None:
            self._temp.cleanup()


def _field_value(field: Field, cast: bool):
    if not cast:
        return field.get_str()
    field_type = field.type()
    if field_type == DataType.CSV_NULL:
        return None
    if DataType.CSV_INT8 <= field_type <= DataType.CSV_INT64:
        return field.get_int()
    if field_type == DataType.CSV_DOUBLE:
        return field.get_double()
    return field.get_str()


def reader(csvfile, dialect="excel", **fmtparams) -> _Reader:
    if dialect != "excel":
        raise NotImplementedError("csvpy.reader currently supports only the default excel dialect")
    return _Reader(csvfile, **fmtparams)


class DictReader:
    def __init__(
        self,
        csvfile,
        fieldnames: Optional[Sequence[str]] = None,
        *,
        restkey=None,
        restval=None,
        dialect: str = "excel",
        cast: bool = False,
        typed: Optional[bool] = None,
        **fmtparams,
    ):
        self.fieldnames = list(fieldnames) if fieldnames is not None else None
        self.restkey = restkey
        self.restval = restval
        self.reader = reader(csvfile, dialect=dialect, cast=cast, typed=typed, **fmtparams)

    def __iter__(self) -> "DictReader":
        return self

    def __next__(self) -> dict:
        if self.fieldnames is None:
            self.fieldnames = next(self.reader)
        row = next(self.reader)
        result = dict(zip(self.fieldnames, row))
        if len(row) > len(self.fieldnames):
            result[self.restkey] = row[len(self.fieldnames):]
        for key in self.fieldnames[len(row):]:
            result[key] = self.restval
        return result
