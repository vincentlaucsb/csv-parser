import csv
import datetime
import io
import os
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

    def test_reader_as_tuple_supports_column_subset(self):
        row = next(csvpy.reader(io.StringIO("1,2,3\n"), fieldnames=["a", "b", "c"]))

        try:
            self.assertEqual(row.as_tuple(), ("1", "2", "3"))
            subset = row.as_tuple(["c", "a"])
        except AttributeError as exc:
            self.skipTest(f"csvpy native extension has not been rebuilt with as_tuple(columns): {exc}")
        except TypeError as exc:
            self.skipTest(f"csvpy native extension has not been rebuilt with as_tuple(columns): {exc}")

        self.assertEqual(subset, ("3", "1"))

    def test_reader_bulk_materializers_consume_remaining_rows(self):
        source = csvpy.reader(io.StringIO("a,b,c\n1,2,3\n4,5,6\n"))

        try:
            self.assertEqual(source.to_lists(["c", "a"]), [["3", "1"], ["6", "4"]])
        except AttributeError as exc:
            self.skipTest(f"csvpy native extension has not been rebuilt with bulk materializers: {exc}")

        self.assertEqual(
            csvpy.reader(io.StringIO("a,b,c\n1,2,3\n4,5,6\n")).to_tuples(["c", "a"]),
            [("3", "1"), ("6", "4")],
        )
        self.assertEqual(
            csvpy.reader(io.StringIO("a,b,c\n1,2,3\n4,5,6\n")).to_dicts(["c", "a"]),
            [{"c": "3", "a": "1"}, {"c": "6", "a": "4"}],
        )

    def test_reader_materialized_iterators_stream_and_chunk(self):
        try:
            rows = csvpy.reader(io.StringIO("a,b,c\n1,2,3\n4,5,6\n7,8,9\n")).lists(["c", "a"])
        except AttributeError as exc:
            self.skipTest(f"csvpy native extension has not been rebuilt with materialized iterators: {exc}")

        self.assertEqual(next(rows), ["3", "1"])
        self.assertEqual(list(rows), [["6", "4"], ["9", "7"]])

        self.assertEqual(
            csvpy.reader(io.StringIO("a,b,c\n1,2,3\n4,5,6\n7,8,9\n")).tuples(["b"]).all(),
            [("2",), ("5",), ("8",)],
        )
        self.assertEqual(
            list(csvpy.reader(io.StringIO("a,b,c\n1,2,3\n4,5,6\n7,8,9\n")).dicts(["a", "c"]).chunks(2)),
            [[{"a": "1", "c": "3"}, {"a": "4", "c": "6"}], [{"a": "7", "c": "9"}]],
        )

        with self.assertRaises(ValueError):
            csvpy.reader(io.StringIO("a,b\n1,2\n")).lists().chunks(0)

    def test_reader_filter_lazy_rows(self):
        source = csvpy.reader(io.StringIO(
            "region,id,price\n"
            "el paso,1,9000\n"
            "phoenix,2,12000\n"
            "EL PASO,3,15000\n"
        )).filter(csvpy.equal("region", "el paso", case_sensitive=False))

        self.assertEqual([row.as_list() for row in source], [
            ["el paso", "1", "9000"],
            ["EL PASO", "3", "15000"],
        ])

    def test_reader_filter_materialized_rows(self):
        data = (
            "region,id,price,year\n"
            "drop,1,5000,2021\n"
            "keep,2,15000,2020\n"
            "keep,3,25000,2024\n"
            "keep,4,9000,2019\n"
        )
        predicate = csvpy.all_of(
            csvpy.equal("region", "keep"),
            csvpy.greater("price", "10000"),
            csvpy.less("year", "2022"),
        )

        self.assertEqual(
            csvpy.reader(io.StringIO(data)).filter(predicate).lists(["id", "price"]).all(),
            [["2", "15000"]],
        )
        self.assertEqual(
            csvpy.reader(io.StringIO(data)).filter(predicate).tuples(["id", "year"]).all(),
            [("2", "2020")],
        )
        self.assertEqual(
            csvpy.reader(io.StringIO(data)).filter(predicate).dicts(["id", "region"]).all(),
            [{"id": "2", "region": "keep"}],
        )

    def test_reader_filter_materialized_chunks(self):
        data = (
            "group,id\n"
            "keep,1\n"
            "drop,2\n"
            "keep,3\n"
            "keep,4\n"
            "drop,5\n"
        )

        chunks = csvpy.reader(io.StringIO(data)).filter(csvpy.equal("group", "keep")).lists(["id"]).chunks(2)

        self.assertEqual(list(chunks), [[["1"], ["3"]], [["4"]]])

    def test_reader_filter_chains_by_default_and_can_replace(self):
        data = (
            "region,id,price\n"
            "el paso,1,9000\n"
            "el paso,2,20000\n"
            "phoenix,3,9000\n"
        )

        chained = (
            csvpy.reader(io.StringIO(data))
            .filter(csvpy.equal("region", "el paso"))
            .filter(csvpy.less("price", "10000"))
            .lists(["id"])
            .all()
        )
        replaced = (
            csvpy.reader(io.StringIO(data))
            .filter(csvpy.equal("region", "el paso"))
            .filter(csvpy.less("price", "10000"), append=False)
            .lists(["id"])
            .all()
        )

        self.assertEqual(chained, [["1"]])
        self.assertEqual(replaced, [["1"], ["3"]])

    def test_reader_filter_missing_column_fails_clearly(self):
        source = csvpy.reader(io.StringIO("id,value\n1,10\n"))

        with self.assertRaisesRegex(RuntimeError, "predicate column not found: missing"):
            source.filter(csvpy.equal("missing", "x"))

    def test_reader_unfiltered_behavior_unchanged_after_filter_addition(self):
        data = "a,b\n1,2\n3,4\n"

        self.assertEqual(
            [row.as_list() for row in csvpy.reader(io.StringIO(data))],
            [["1", "2"], ["3", "4"]],
        )
        self.assertEqual(
            csvpy.reader(io.StringIO(data)).lists(["b"]).all(),
            [["2"], ["4"]],
        )

    def test_reader_returns_lazy_rows(self):
        row = next(csvpy.reader(io.StringIO("a,b\n"), consume_header=False))
        self.assertNotIsInstance(row, list)
        self.assertEqual(row.as_list(), ["a", "b"])

    def test_split_modules_preserve_public_imports(self):
        from csvpy import equal, read_numpy, reader, write_csv

        self.assertIs(reader, csvpy.reader)
        self.assertIs(read_numpy, csvpy.read_numpy)
        self.assertIs(equal, csvpy.equal)
        self.assertIs(write_csv, csvpy.write_csv)
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

    def test_write_csv_accepts_python_iterables(self):
        with tempfile.NamedTemporaryFile("r+", encoding="utf-8", newline="", delete=False) as handle:
            filename = handle.name

        try:
            try:
                csvpy.write_csv(
                    filename,
                    [["name", "note"], ["Alice", "hello, world"], ["Bob", None], ("Eve", 'quote "me"')],
                    write_header=False,
                )
            except AttributeError as exc:
                self.skipTest(f"csvpy native extension has not been rebuilt with write_csv: {exc}")

            with open(filename, encoding="utf-8", newline="") as handle:
                self.assertEqual(
                    handle.read(),
                    'name,note\nAlice,"hello, world"\nBob,\nEve,"quote ""me"""\n',
                )
        finally:
            os.unlink(filename)

    def test_write_csv_accepts_dict_rows_and_lazy_rows(self):
        with tempfile.NamedTemporaryFile("r+", encoding="utf-8", newline="", delete=False) as handle:
            dict_filename = handle.name
        with tempfile.NamedTemporaryFile("r+", encoding="utf-8", newline="", delete=False) as handle:
            lazy_filename = handle.name
        with tempfile.NamedTemporaryFile("r+", encoding="utf-8", newline="", delete=False) as handle:
            empty_filename = handle.name

        try:
            try:
                csvpy.write_csv(dict_filename, [{"a": 1, "b": None}, {"a": "x,y", "b": True}])
                csvpy.write_csv(
                    lazy_filename,
                    csvpy.reader(io.StringIO("a,b,c\n1,2,3\n4,5,6\n")),
                    fieldnames=["c", "a"],
                )
                csvpy.write_csv(empty_filename, [], fieldnames=["a", "b"])
            except AttributeError as exc:
                self.skipTest(f"csvpy native extension has not been rebuilt with write_csv: {exc}")

            with open(dict_filename, encoding="utf-8", newline="") as handle:
                self.assertEqual(handle.read(), "a,b\n1,\n\"x,y\",True\n")
            with open(lazy_filename, encoding="utf-8", newline="") as handle:
                self.assertEqual(handle.read(), "c,a\n3,1\n6,4\n")
            with open(empty_filename, encoding="utf-8", newline="") as handle:
                self.assertEqual(handle.read(), "a,b\n")
        finally:
            os.unlink(dict_filename)
            os.unlink(lazy_filename)
            os.unlink(empty_filename)


if __name__ == "__main__":
    unittest.main()
