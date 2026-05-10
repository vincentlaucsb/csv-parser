import importlib.machinery as _machinery
import importlib.util as _util
import os as _os
import sys as _sys

__path__ = [_os.path.dirname(__file__)]


def _load_native_module():
    module_name = __name__ + ".csvpy"
    existing = _sys.modules.get(module_name)
    if existing is not None:
        return existing

    for suffix in _machinery.EXTENSION_SUFFIXES:
        path = _os.path.join(__path__[0], "csvpy" + suffix)
        if not _os.path.exists(path):
            continue

        spec = _util.spec_from_file_location(module_name, path)
        if spec is None or spec.loader is None:
            continue

        module = _util.module_from_spec(spec)
        _sys.modules[module_name] = module
        spec.loader.exec_module(module)
        return module

    raise ImportError("cannot find the csvpy native extension next to the Python package")


_native = _load_native_module()

CSVFileInfo = _native.CSVFileInfo
DataType = _native.DataType
Field = _native.Field
Format = _native.Format
Reader = _native.Reader
Row = _native.Row
VariableColumnPolicy = _native.VariableColumnPolicy
csv_data_types = _native.csv_data_types
equal = _native.equal
get_col_pos = _native.get_col_pos
get_file_info = _native.get_file_info
parse = _native.parse
parse_no_header = _native.parse_no_header
read_numpy = _native.read_numpy
from ._dict_reader import DictReader
from ._document import CSVDocument
from ._reader import reader, rows

__all__ = [
    "CSVFileInfo",
    "CSVDocument",
    "DataType",
    "DictReader",
    "Field",
    "Format",
    "Reader",
    "Row",
    "VariableColumnPolicy",
    "csv_data_types",
    "equal",
    "get_col_pos",
    "get_file_info",
    "parse",
    "parse_no_header",
    "read_numpy",
    "reader",
    "rows",
]
