"""Dictionary row facade for csvpy.reader()."""

from __future__ import annotations

from typing import Optional, Sequence

from ._reader import reader


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
            self.fieldnames = list(next(self.reader))
        row = next(self.reader)
        result = dict(zip(self.fieldnames, row))
        if len(row) > len(self.fieldnames):
            result[self.restkey] = row[len(self.fieldnames):]
        for key in self.fieldnames[len(row):]:
            result[key] = self.restval
        return result
