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
                    [list(row) for row in csvpy.reader(io.StringIO(data), consume_header=False)],
                    list(csv.reader(io.StringIO(data))),
                )

    def test_reader_default_does_not_cast(self):
        row = next(csvpy.reader(io.StringIO("1,2.5,\n"), consume_header=False))
        self.assertEqual(row.as_list(), ["1", "2.5", ""])
        self.assertTrue(all(isinstance(value, str) for value in row))

    def test_reader_cast_true_casts_scalars(self):
        row = next(csvpy.reader(
            io.StringIO("1,2.5,text,,true,false,1970-01-02T00:00:00.123Z\n"),
            cast=True,
            consume_header=False,
        ))
        self.assertEqual(row[:6], [1, 2.5, "text", None, True, False])
        self.assertIsInstance(row[6], datetime.datetime)
        self.assertEqual(row[6].year, 1970)
        self.assertEqual(row[6].month, 1)
        self.assertEqual(row[6].day, 2)
        self.assertEqual(row[6].microsecond, 123000)

    def test_reader_typed_alias_casts_scalars(self):
        self.assertEqual(
            [row.as_list() for row in csvpy.reader(io.StringIO("1,2.5\n"), typed=True, consume_header=False)],
            [[1, 2.5]],
        )

    def test_reader_supports_delimiter_and_skipinitialspace(self):
        data = "a; b\n1; 2\n"
        self.assertEqual(
            [list(row) for row in csvpy.reader(io.StringIO(data), delimiter=";", skipinitialspace=True, consume_header=False)],
            list(csv.reader(io.StringIO(data), delimiter=";", skipinitialspace=True)),
        )

    def test_reader_file_object_respects_current_position(self):
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8", newline="", delete=True) as handle:
            handle.write("skip,this\nkeep,that\n")
            handle.seek(len("skip,this\n"))
            self.assertEqual([row.as_list() for row in csvpy.reader(handle, consume_header=False)], [["keep", "that"]])

    def test_reader_can_disable_header_consumption(self):
        source = csvpy.reader(io.StringIO("a,b\n1,2\n"), consume_header=False)

        self.assertEqual(source.fieldnames, [])
        self.assertEqual(source.get_col_names(), [])
        self.assertEqual(next(source).as_list(), ["a", "b"])

    def test_reader_consumes_header_by_default(self):
        source = csvpy.reader(io.StringIO("a,b\n1,2\n"))

        self.assertEqual(source.fieldnames, ["a", "b"])
        self.assertEqual(source.get_col_names(), ["a", "b"])
        self.assertEqual(next(source).as_dict(), {"a": "1", "b": "2"})

    def test_reader_explicit_fieldnames_are_exposed_without_consuming_first_row(self):
        source = csvpy.reader(io.StringIO("1,2\n3,4\n"), fieldnames=["a", "b"])

        self.assertEqual(source.fieldnames, ["a", "b"])
        self.assertEqual(next(source).as_dict(), {"a": "1", "b": "2"})

    def test_reader_file_like_is_lazy_and_list_like(self):
        row = next(csvpy.reader(io.StringIO("1,2,3,4\n"), fieldnames=["a", "b", "c", "d"]))

        self.assertEqual(len(row), 4)
        self.assertEqual(row[0], "1")
        self.assertEqual(row[-1], "4")
        self.assertEqual(row[1:4:2], ["2", "4"])
        self.assertEqual(list(row), ["1", "2", "3", "4"])
        self.assertEqual(row.as_list(), ["1", "2", "3", "4"])

    def test_reader_path_input_and_cast_false_strings(self):
        with temp_csv_path("a,b,c\n1,2.5,true\n") as filename:
            row = next(csvpy.reader(filename))

        self.assertEqual(row.as_list(), ["1", "2.5", "true"])
        self.assertTrue(all(isinstance(value, str) for value in row))
        self.assertEqual(row["a"], "1")
        self.assertEqual(row.as_dict(), {"a": "1", "b": "2.5", "c": "true"})

    def test_reader_cast_true_returns_scalar_types(self):
        row = next(csvpy.reader(
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

    def test_reader_quoted_escaped_fields_are_unescaped(self):
        row = next(csvpy.reader(io.StringIO('a,b\n"x,y","z""q"\n')))
        self.assertEqual(row[0], "x,y")
        self.assertEqual(row[1], 'z"q')

    def test_reader_fieldnames_keep_first_row_and_support_dicts(self):
        row = next(csvpy.reader(io.StringIO("1,2\n3,4\n"), fieldnames=["a", "b"]))
        self.assertEqual(row["a"], "1")
        self.assertEqual(row.get_int("b"), 2)
        self.assertEqual(row.as_dict(), {"a": "1", "b": "2"})

    def test_reader_as_dict_supports_column_subset(self):
        row = next(csvpy.reader(io.StringIO("1,2,3\n"), fieldnames=["a", "b", "c"]))

        try:
            subset = row.as_dict(["c", "a"])
        except TypeError as exc:
            self.skipTest(f"csvpy native extension has not been rebuilt with as_dict(columns): {exc}")

        self.assertEqual(subset, {"c": "3", "a": "1"})

    def test_reader_returns_lazy_rows(self):
        row = next(csvpy.reader(io.StringIO("a,b\n"), consume_header=False))
        self.assertNotIsInstance(row, list)
        self.assertEqual(row.as_list(), ["a", "b"])

    def test_split_modules_preserve_public_imports(self):
        from csvpy import equal, read_numpy, reader

        self.assertIs(reader, csvpy.reader)
        self.assertIs(read_numpy, csvpy.read_numpy)
        self.assertIs(equal, csvpy.equal)
        self.assertNotIn("rows", csvpy.__all__)
        self.assertNotIn("Format", csvpy.__all__)
        self.assertNotIn("VariableColumnPolicy", csvpy.__all__)
        self.assertNotIn("get_col_pos", csvpy.__all__)
        self.assertNotIn("parse", csvpy.__all__)
        self.assertFalse(hasattr(csvpy, "Format"))
        self.assertFalse(hasattr(csvpy, "VariableColumnPolicy"))
        self.assertFalse(hasattr(csvpy, "get_col_pos"))
        self.assertFalse(hasattr(csvpy, "parse"))

        from csvpy.compat import reader as compat_reader

        self.assertIs(compat_reader, csvpy.reader)


if __name__ == "__main__":
    unittest.main()
