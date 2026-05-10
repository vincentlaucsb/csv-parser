"""Compatibility imports for the split csvpy Python facade."""

from ._dict_reader import DictReader
from ._reader import reader, rows

__all__ = ["DictReader", "reader", "rows"]
