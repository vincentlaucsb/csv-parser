from .csvpy import Reader, DataType

class DictReader:
    def __init__(self, filename):
        self._reader = Reader(filename)
        self._csvIterator = self._reader.__iter__()

    def __iter__(self):
        return self

    def __next__(self):
        ret = dict()
        next_row = self._csvIterator.__next__()

        for col_name in next_row.get_col_names():
            field = next_row[col_name]
            field_type = field.type()
            value = None

            if field_type == DataType.CSV_STRING:
                value = field.get_str()
            elif field_type >= DataType.CSV_INT8 and field_type <= DataType.CSV_INT64:
                value = field.get_int()
            elif field_type == DataType.CSV_DOUBLE:
                value = field.get_double()

            ret[col_name] = value

        return ret