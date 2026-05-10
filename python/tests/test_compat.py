import csv
import datetime
import io
import tempfile
import unittest

import csvpy

from tempfiles import temp_csv_path


CASES = [
    "a,b\n1,2\n",
    'a,"b,c","d""e"\n',
    '"a\nb",c\n',
    "a,,c\n",
]


class CompatReaderTests(unittest.TestCase):
    def test_reader_matches_stdlib_for_common_cases(self):
        for data in CASES:
            with self.subTest(data=data):
                self.assertEqual(
                    [list(row) for row in csvpy.reader(io.StringIO(data))],
                    list(csv.reader(io.StringIO(data))),
                )

    def test_reader_default_does_not_cast(self):
        row = next(csvpy.reader(io.StringIO("1,2.5,\n")))
        self.assertEqual(row.as_list(), ["1", "2.5", ""])
        self.assertTrue(all(isinstance(value, str) for value in row))

    def test_reader_cast_true_casts_scalars(self):
        row = next(csvpy.reader(
            io.StringIO("1,2.5,text,,true,false,1970-01-02T00:00:00.123Z\n"),
            cast=True,
        ))
        self.assertEqual(row[:6], [1, 2.5, "text", None, True, False])
        self.assertIsInstance(row[6], datetime.datetime)
        self.assertEqual(row[6].year, 1970)
        self.assertEqual(row[6].month, 1)
        self.assertEqual(row[6].day, 2)
        self.assertEqual(row[6].microsecond, 123000)

    def test_reader_typed_alias_casts_scalars(self):
        self.assertEqual(
            [row.as_list() for row in csvpy.reader(io.StringIO("1,2.5\n"), typed=True)],
            [[1, 2.5]],
        )

    def test_reader_supports_delimiter_and_skipinitialspace(self):
        data = "a; b\n1; 2\n"
        self.assertEqual(
            [list(row) for row in csvpy.reader(io.StringIO(data), delimiter=";", skipinitialspace=True)],
            list(csv.reader(io.StringIO(data), delimiter=";", skipinitialspace=True)),
        )

    def test_reader_file_object_respects_current_position(self):
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8", newline="", delete=True) as handle:
            handle.write("skip,this\nkeep,that\n")
            handle.seek(len("skip,this\n"))
            self.assertEqual([row.as_list() for row in csvpy.reader(handle)], [["keep", "that"]])

    def test_dict_reader_uses_header_and_strings_by_default(self):
        rows = list(csvpy.DictReader(io.StringIO("a,b\n1,2.5\n")))
        self.assertEqual(rows, [{"a": "1", "b": "2.5"}])
        self.assertTrue(all(isinstance(value, str) for value in rows[0].values()))

    def test_dict_reader_explicit_fieldnames_keeps_first_row(self):
        rows = list(csvpy.DictReader(io.StringIO("1,2\n3,4\n"), fieldnames=["a", "b"]))
        self.assertEqual(rows, [{"a": "1", "b": "2"}, {"a": "3", "b": "4"}])

    def test_dict_reader_cast_true(self):
        self.assertEqual(
            list(csvpy.DictReader(io.StringIO("a,b,c\n1,2.5,\n"), cast=True)),
            [{"a": 1, "b": 2.5, "c": None}],
        )

    def test_rows_file_like_is_lazy_and_list_like(self):
        row = next(csvpy.rows(io.StringIO("1,2,3,4\n"), fieldnames=["a", "b", "c", "d"]))

        self.assertEqual(len(row), 4)
        self.assertEqual(row[0], "1")
        self.assertEqual(row[-1], "4")
        self.assertEqual(row[1:4:2], ["2", "4"])
        self.assertEqual(list(row), ["1", "2", "3", "4"])
        self.assertEqual(row.as_list(), ["1", "2", "3", "4"])

    def test_rows_path_input_and_cast_false_strings(self):
        with temp_csv_path("a,b,c\n1,2.5,true\n") as filename:
            row = next(csvpy.rows(filename))

        self.assertEqual(row.as_list(), ["1", "2.5", "true"])
        self.assertTrue(all(isinstance(value, str) for value in row))
        self.assertEqual(row["a"], "1")
        self.assertEqual(row.as_dict(), {"a": "1", "b": "2.5", "c": "true"})

    def test_rows_cast_true_returns_scalar_types(self):
        row = next(csvpy.rows(
            io.StringIO("id,amount,name,empty,active,when\n1,2.5,text,,true,1970-01-02T00:00:00.123Z\n"),
            cast=True,
        ))

        self.assertEqual(row.as_list()[:5], [1, 2.5, "text", None, True])
        self.assertIsInstance(row[5], datetime.datetime)
        self.assertEqual(row[5].day, 2)
        self.assertEqual(row.get_int("id"), 1)
        self.assertEqual(row.get_float("amount"), 2.5)
        self.assertTrue(row.get_bool("active"))
        self.assertEqual(row.get_str("name"), "text")
        self.assertEqual(row.type("active"), csvpy.DataType.CSV_BOOL)

    def test_rows_quoted_escaped_fields_are_unescaped(self):
        row = next(csvpy.rows(io.StringIO('a,b\n"x,y","z""q"\n')))
        self.assertEqual(row[0], "x,y")
        self.assertEqual(row[1], 'z"q')

    def test_rows_fieldnames_keep_first_row_and_support_dicts(self):
        row = next(csvpy.rows(io.StringIO("1,2\n3,4\n"), fieldnames=["a", "b"]))
        self.assertEqual(row["a"], "1")
        self.assertEqual(row.get_int("b"), 2)
        self.assertEqual(row.as_dict(), {"a": "1", "b": "2"})

    def test_reader_returns_lazy_rows(self):
        row = next(csvpy.reader(io.StringIO("a,b\n")))
        self.assertNotIsInstance(row, list)
        self.assertEqual(row.as_list(), ["a", "b"])

    def test_split_modules_preserve_public_imports(self):
        from csvpy import DictReader, equal, read_numpy, reader, rows

        self.assertIs(reader, csvpy.reader)
        self.assertIs(rows, csvpy.rows)
        self.assertIs(DictReader, csvpy.DictReader)
        self.assertIs(read_numpy, csvpy.read_numpy)
        self.assertIs(equal, csvpy.equal)

        from csvpy.DictReader import DictReader as legacy_dict_reader
        from csvpy.compat import DictReader as compat_dict_reader
        from csvpy.compat import reader as compat_reader
        from csvpy.compat import rows as compat_rows

        self.assertIs(compat_reader, csvpy.reader)
        self.assertIs(compat_rows, csvpy.rows)
        self.assertIs(compat_dict_reader, DictReader)
        self.assertIs(legacy_dict_reader, DictReader)


if __name__ == "__main__":
    unittest.main()
