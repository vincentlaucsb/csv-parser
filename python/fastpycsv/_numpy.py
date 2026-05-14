"""NumPy export facade sharing reader() CSV format handling."""

from __future__ import annotations

from typing import Optional, Sequence

from ._format import _make_format
from ._reader import _CSVSource
from .fastpycsv import _read_numpy as _native_read_numpy
from .fastpycsv import _read_numpy_batches as _native_read_numpy_batches


def _numpy_format(
    delimiter: str,
    quotechar: Optional[str],
    doublequote: bool,
    skipinitialspace: bool,
    strict: bool,
    consume_header: bool,
    fieldnames: Optional[Sequence[str]],
):
    return _make_format(
        delimiter,
        quotechar,
        doublequote,
        skipinitialspace,
        strict,
        fieldnames=fieldnames,
        no_header=(not consume_header) or fieldnames is not None,
    )


def read_numpy(
    csvfile,
    columns=None,
    *,
    cast: bool = True,
    predicate=None,
    member: Optional[str] = None,
    delimiter: str = ",",
    quotechar: Optional[str] = '"',
    doublequote: bool = True,
    skipinitialspace: bool = False,
    strict: bool = False,
    consume_header: bool = True,
    fieldnames: Optional[Sequence[str]] = None,
):
    fmt = _numpy_format(
        delimiter,
        quotechar,
        doublequote,
        skipinitialspace,
        strict,
        consume_header,
        fieldnames,
    )
    source = _CSVSource(csvfile, member)
    try:
        return _native_read_numpy(source.name, fmt, columns, cast, predicate)
    finally:
        source.cleanup()


def read_numpy_batches(
    csvfile,
    columns=None,
    *,
    predicate=None,
    cast: bool = True,
    batch_size: int = 50000,
    schema: str = "sample",
    member: Optional[str] = None,
    delimiter: str = ",",
    quotechar: Optional[str] = '"',
    doublequote: bool = True,
    skipinitialspace: bool = False,
    strict: bool = False,
    consume_header: bool = True,
    fieldnames: Optional[Sequence[str]] = None,
):
    fmt = _numpy_format(
        delimiter,
        quotechar,
        doublequote,
        skipinitialspace,
        strict,
        consume_header,
        fieldnames,
    )
    source = _CSVSource(csvfile, member)
    try:
        reader = _native_read_numpy_batches(source.name, fmt, columns, predicate, cast, batch_size, schema)
    except Exception:
        source.cleanup()
        raise
    return _NumpyBatchIterator(reader, source)


class _NumpyBatchIterator:
    def __init__(self, reader, source: _CSVSource):
        self._reader = reader
        self._source = source

    def __iter__(self):
        return self

    def __next__(self):
        try:
            return next(self._reader)
        except StopIteration:
            self._source.cleanup()
            raise

    def __del__(self):
        source = getattr(self, "_source", None)
        if source is not None:
            source.cleanup()
