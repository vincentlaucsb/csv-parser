"""Python facade for C++-owned CSV document workflows."""

from __future__ import annotations

from typing import Optional, Sequence

from ._format import _make_format
from ._reader import _CSVSource
from .csvpy import _CSVDocument


class CSVDocument:
    """C++-owned CSV rows with deferred row deletion and NumPy export."""

    def __init__(
        self,
        path_or_rows,
        *,
        delimiter: str = ",",
        quotechar: Optional[str] = '"',
        doublequote: bool = True,
        skipinitialspace: bool = False,
        strict: bool = False,
        cast: bool = False,
        typed: Optional[bool] = None,
        fieldnames: Optional[Sequence[str]] = None,
        no_header: bool = False,
    ):
        if typed is not None:
            cast = typed
        self._source = _CSVSource(path_or_rows)
        fmt = _make_format(
            delimiter,
            quotechar,
            doublequote,
            skipinitialspace,
            strict,
            fieldnames=fieldnames,
            no_header=no_header or fieldnames is not None,
        )
        self._document = _CSVDocument(self._source.name, fmt, cast)

    @property
    def pending_deletes(self) -> bool:
        return self._document.pending_deletes

    def __len__(self) -> int:
        return len(self._document)

    def __iter__(self):
        return iter(self._document)

    def __getitem__(self, index):
        return self._document[index]

    def materialize_deletes(self) -> int:
        return self._document.materialize_deletes()

    def discard_deletes(self) -> int:
        return self._document.discard_deletes()

    def to_numpy(self, columns=None, cast: bool = True, predicate=None):
        return self._document.to_numpy(columns=columns, cast=cast, predicate=predicate)

    def delete_where(self, predicate) -> int:
        return self._document.delete_where(predicate)

    def __del__(self):
        source = getattr(self, "_source", None)
        if source is not None:
            source.cleanup()
