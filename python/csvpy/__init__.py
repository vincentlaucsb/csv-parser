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
Reader = _native.Reader
Row = _native.Row
csv_data_types = _native.csv_data_types
all_of = _native.all_of
equal = _native.equal
greater = _native.greater
greater_equal = _native.greater_equal
get_file_info = _native.get_file_info
less = _native.less
less_equal = _native.less_equal
parse_no_header = _native.parse_no_header
from ._numpy import read_numpy, read_numpy_batches
from ._reader import reader
from ._writer import write_csv

__all__ = [
    "CSVFileInfo",
    "DataType",
    "Field",
    "Reader",
    "Row",
    "all_of",
    "csv_data_types",
    "equal",
    "greater",
    "greater_equal",
    "get_file_info",
    "less",
    "less_equal",
    "parse_no_header",
    "read_numpy",
    "read_numpy_batches",
    "reader",
    "write_csv",
]
