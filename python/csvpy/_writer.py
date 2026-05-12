"""CSV writer facade over the native csvpy extension."""

from __future__ import annotations

import os
from typing import Iterable, Optional, Sequence

from . import csvpy as _native


def write_csv(
    csvfile,
    rows: Iterable[object],
    *,
    fieldnames: Optional[Sequence[object]] = None,
    write_header: bool = True,
    quote_minimal: bool = True,
) -> None:
    """Write rows to a CSV file.

    `rows` may contain lazy csvpy rows, dictionaries, or ordinary Python
    iterables. Fields are stringified before writing; `None` becomes an empty
    CSV field.
    """

    if hasattr(csvfile, "write"):
        write = getattr(_native, "_write_csv_filelike", None)
        if write is None:
            raise AttributeError("csvpy native extension has not been rebuilt with file-like write_csv support")

        write(csvfile, rows, fieldnames, write_header, quote_minimal)
        return

    write = getattr(_native, "_write_csv", None)
    if write is None:
        raise AttributeError("csvpy native extension has not been rebuilt with write_csv support")

    write(os.fspath(csvfile), rows, fieldnames, write_header, quote_minimal)
