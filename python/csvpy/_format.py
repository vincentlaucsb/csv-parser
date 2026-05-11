"""CSVFormat construction helpers for the public Python facade."""

from __future__ import annotations

from typing import Optional, Sequence

from .csvpy import Format as _NativeFormat
from .csvpy import VariableColumnPolicy as _NativeVariableColumnPolicy


def _make_format(
    delimiter: str = ",",
    quotechar: Optional[str] = '"',
    doublequote: bool = True,
    skipinitialspace: bool = False,
    strict: bool = False,
    fieldnames: Optional[Sequence[str]] = None,
    no_header: bool = True,
) -> _NativeFormat:
    if len(delimiter) != 1:
        raise TypeError("delimiter must be a one-character string")
    if quotechar is not None and len(quotechar) != 1:
        raise TypeError("quotechar must be a one-character string or None")
    if not doublequote:
        raise NotImplementedError("csvpy.reader does not support doublequote=False")

    fmt = _NativeFormat().delimiter(delimiter)
    if no_header:
        fmt.no_header()
    if quotechar is None:
        fmt.quote(False)
    else:
        fmt.quote(quotechar)
    if skipinitialspace:
        fmt.trim([" "])
    if strict:
        fmt.variable_columns(_NativeVariableColumnPolicy.THROW)
    if fieldnames is not None:
        fmt.column_names(list(fieldnames))
    return fmt
