import csv
import datetime
import io
import tempfile
import unittest

import csvpy


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
                    list(csvpy.reader(io.StringIO(data))),
                    list(csv.reader(io.StringIO(data))),
                )

    def test_reader_default_does_not_cast(self):
        rows = list(csvpy.reader(io.StringIO("1,2.5,\n")))
        self.assertEqual(rows, [["1", "2.5", ""]])
        self.assertTrue(all(isinstance(value, str) for value in rows[0]))

    def test_reader_cast_true_casts_scalars(self):
        rows = list(csvpy.reader(
            io.StringIO("1,2.5,text,,true,false,1970-01-02T00:00:00.123Z\n"),
            cast=True,
        ))
        self.assertEqual(rows[0][:6], [1, 2.5, "text", None, True, False])
        self.assertIsInstance(rows[0][6], datetime.datetime)
        self.assertEqual(rows[0][6].year, 1970)
        self.assertEqual(rows[0][6].month, 1)
        self.assertEqual(rows[0][6].day, 2)
        self.assertEqual(rows[0][6].microsecond, 123000)

    def test_reader_typed_alias_casts_scalars(self):
        self.assertEqual(list(csvpy.reader(io.StringIO("1,2.5\n"), typed=True)), [[1, 2.5]])

    def test_reader_supports_delimiter_and_skipinitialspace(self):
        data = "a; b\n1; 2\n"
        self.assertEqual(
            list(csvpy.reader(io.StringIO(data), delimiter=";", skipinitialspace=True)),
            list(csv.reader(io.StringIO(data), delimiter=";", skipinitialspace=True)),
        )

    def test_reader_file_object_respects_current_position(self):
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8", newline="", delete=True) as handle:
            handle.write("skip,this\nkeep,that\n")
            handle.seek(len("skip,this\n"))
            self.assertEqual(list(csvpy.reader(handle)), [["keep", "that"]])

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


if __name__ == "__main__":
    unittest.main()
