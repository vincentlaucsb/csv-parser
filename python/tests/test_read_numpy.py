import math
import unittest

import csvpy

from tempfiles import temp_csv_path

try:
    import numpy as np
except Exception:  # pragma: no cover - depends on optional environment
    np = None

try:
    import pandas as pd
except Exception:  # pragma: no cover - depends on optional environment
    pd = None


def concatenate_batches(batches):
    first = batches[0]
    return {
        column: np.concatenate([batch[column] for batch in batches])
        for column in first
    }


@unittest.skipIf(np is None, "NumPy is required for csvpy.read_numpy tests")
class ReadNumpyTests(unittest.TestCase):
    def test_read_numpy_materializes_typed_arrays(self):
        path = self.enterContext(temp_csv_path(
            "name,count,ratio,flag\n"
            "alpha,1,1.5,true\n"
            "beta,2,2.25,false\n"
        ))

        arrays = csvpy.read_numpy(path)

        self.assertEqual(set(arrays), {"name", "count", "ratio", "flag"})
        self.assertEqual(arrays["count"].dtype, np.dtype("int64"))
        self.assertEqual(arrays["ratio"].dtype, np.dtype("float64"))
        self.assertEqual(arrays["flag"].dtype, np.dtype("bool"))
        self.assertIsInstance(arrays["name"].dtype, np.dtypes.StringDType)
        self.assertEqual(arrays["count"].tolist(), [1, 2])
        self.assertEqual(arrays["ratio"].tolist(), [1.5, 2.25])
        self.assertEqual(arrays["flag"].tolist(), [True, False])
        self.assertEqual(arrays["name"].tolist(), ["alpha", "beta"])

    def test_read_numpy_widens_nullable_int_float_and_bool(self):
        path = self.enterContext(temp_csv_path(
            "id,score,enabled\n"
            "1,2.5,true\n"
            ",,false\n"
            "3,4.5,\n"
        ))

        arrays = csvpy.read_numpy(path)

        self.assertEqual(arrays["id"].dtype, np.dtype("float64"))
        self.assertEqual(arrays["score"].dtype, np.dtype("float64"))
        self.assertEqual(arrays["enabled"].dtype, np.dtype("float64"))
        self.assertTrue(math.isnan(arrays["id"][1]))
        self.assertTrue(math.isnan(arrays["score"][1]))
        self.assertEqual(arrays["enabled"][0], 1.0)
        self.assertEqual(arrays["enabled"][1], 0.0)
        self.assertTrue(math.isnan(arrays["enabled"][2]))

    def test_read_numpy_selected_columns_and_escaped_strings(self):
        path = self.enterContext(temp_csv_path(
            "id,text,skip\n"
            "1,\"a,b\",x\n"
            "2,\"c\"\"d\",y\n"
        ))

        arrays = csvpy.read_numpy(path, columns=["text", "id"])

        self.assertEqual(list(arrays), ["text", "id"])
        self.assertEqual(arrays["text"].tolist(), ["a,b", 'c"d'])
        self.assertEqual(arrays["id"].tolist(), [1, 2])

    def test_read_numpy_preserves_late_string_promotion_text(self):
        path = self.enterContext(temp_csv_path(
            "mixed\n"
            "001\n"
            "2.50\n"
            "plain text\n"
        ))

        arrays = csvpy.read_numpy(path)

        self.assertIsInstance(arrays["mixed"].dtype, np.dtypes.StringDType)
        self.assertEqual(arrays["mixed"].tolist(), ["001", "2.50", "plain text"])

    def test_read_numpy_cast_false_returns_strings(self):
        path = self.enterContext(temp_csv_path(
            "id,flag\n"
            "1,true\n"
            "2,false\n"
        ))

        arrays = csvpy.read_numpy(path, cast=False)

        self.assertIsInstance(arrays["id"].dtype, np.dtypes.StringDType)
        self.assertIsInstance(arrays["flag"].dtype, np.dtypes.StringDType)
        self.assertEqual(arrays["id"].tolist(), ["1", "2"])
        self.assertEqual(arrays["flag"].tolist(), ["true", "false"])

    def test_read_numpy_native_predicate_filters_rows(self):
        path = self.enterContext(temp_csv_path(
            "region,id,value\n"
            "el paso,1,10\n"
            "phoenix,2,20\n"
            "EL PASO,3,30\n"
        ))

        arrays = csvpy.read_numpy(
            path,
            columns=["id", "value"],
            predicate=csvpy.equal("region", "el paso", case_sensitive=False),
        )

        self.assertEqual(arrays["id"].tolist(), [1, 3])
        self.assertEqual(arrays["value"].tolist(), [10, 30])

    def test_read_numpy_native_predicate_preserves_string_replay(self):
        path = self.enterContext(temp_csv_path(
            "region,mixed\n"
            "keep,001\n"
            "drop,2.50\n"
            "keep,plain text\n"
        ))

        arrays = csvpy.read_numpy(path, predicate=csvpy.equal("region", "keep"))

        self.assertIsInstance(arrays["mixed"].dtype, np.dtypes.StringDType)
        self.assertEqual(arrays["region"].tolist(), ["keep", "keep"])
        self.assertEqual(arrays["mixed"].tolist(), ["001", "plain text"])

    def test_read_numpy_native_predicate_parallel_sized_input(self):
        rows = ["group,id\n"]
        expected = []
        for index in range(10000):
            group = "keep" if index % 10 == 0 else "drop"
            rows.append(f"{group},{index}\n")
            if group == "keep":
                expected.append(index)
        path = self.enterContext(temp_csv_path("".join(rows)))

        arrays = csvpy.read_numpy(path, columns=["id"], predicate=csvpy.equal("group", "keep"))

        self.assertEqual(arrays["id"].tolist(), expected)

    def test_read_numpy_native_numeric_comparison_predicates(self):
        path = self.enterContext(temp_csv_path(
            "region,price,year\n"
            "a,5000,2018\n"
            "b,15000,2020\n"
            "c,25000,2024\n"
        ))

        arrays = csvpy.read_numpy(path, columns=["region"], predicate=csvpy.greater("price", "10000"))

        self.assertEqual(arrays["region"].tolist(), ["b", "c"])

        arrays = csvpy.read_numpy(path, columns=["region"], predicate=csvpy.less_equal("year", "2020"))

        self.assertEqual(arrays["region"].tolist(), ["a", "b"])

    def test_all_of_native_predicate_filters_read_numpy_and_batches(self):
        path = self.enterContext(temp_csv_path(
            "region,id,price,year\n"
            "drop,1,5000,2021\n"
            "keep,2,15000,2020\n"
            "keep,3,25000,2024\n"
            "keep,4,9000,2019\n"
        ))
        predicate = csvpy.all_of(
            csvpy.equal("region", "keep"),
            csvpy.greater("price", "10000"),
            csvpy.less_equal("year", "2021"),
        )

        arrays = csvpy.read_numpy(path, columns=["id"], predicate=predicate)
        batches = list(csvpy.read_numpy_batches(
            path,
            columns=["id"],
            predicate=predicate,
            batch_size=2,
            schema="global",
        ))

        self.assertEqual(arrays["id"].tolist(), [2])
        self.assertEqual(concatenate_batches(batches)["id"].tolist(), [2])

    def test_all_of_rejects_invalid_children(self):
        with self.assertRaisesRegex(TypeError, "all_of"):
            csvpy.all_of(csvpy.equal("region", "keep"), object())

        with self.assertRaisesRegex(TypeError, "all_of"):
            csvpy.all_of()

    @unittest.skipIf(pd is None, "pandas is required for DataFrame handoff test")
    def test_read_numpy_result_builds_pandas_dataframe(self):
        path = self.enterContext(temp_csv_path(
            "name,value\n"
            "alpha,1\n"
            "beta,2\n"
        ))

        frame = pd.DataFrame(csvpy.read_numpy(path))

        self.assertEqual(list(frame.columns), ["name", "value"])
        self.assertEqual(frame["name"].tolist(), ["alpha", "beta"])
        self.assertEqual(frame["value"].tolist(), [1, 2])

    def test_read_numpy_batches_emits_multiple_batches_without_predicate(self):
        path = self.enterContext(temp_csv_path(
            "id,value\n"
            "1,10\n"
            "2,20\n"
            "3,30\n"
            "4,40\n"
            "5,50\n"
        ))

        batches = list(csvpy.read_numpy_batches(path, batch_size=2))

        self.assertEqual(len(batches), 3)
        self.assertEqual([batch["id"].tolist() for batch in batches], [[1, 2], [3, 4], [5]])
        self.assertEqual([batch["value"].tolist() for batch in batches], [[10, 20], [30, 40], [50]])

    def test_read_numpy_batches_selected_columns(self):
        path = self.enterContext(temp_csv_path(
            "id,name,value\n"
            "1,alpha,10\n"
            "2,beta,20\n"
            "3,gamma,30\n"
        ))

        batches = list(csvpy.read_numpy_batches(path, columns=["name", "id"], batch_size=2))

        self.assertEqual([list(batch) for batch in batches], [["name", "id"], ["name", "id"]])
        self.assertEqual(concatenate_batches(batches)["name"].tolist(), ["alpha", "beta", "gamma"])
        self.assertEqual(concatenate_batches(batches)["id"].tolist(), [1, 2, 3])

    def test_read_numpy_batches_equality_predicate(self):
        path = self.enterContext(temp_csv_path(
            "region,id\n"
            "el paso,1\n"
            "phoenix,2\n"
            "EL PASO,3\n"
            "tucson,4\n"
        ))

        batches = list(csvpy.read_numpy_batches(
            path,
            ["id"],
            csvpy.equal("region", "el paso", case_sensitive=False),
            True,
            2,
        ))

        self.assertEqual(len(batches), 2)
        self.assertEqual(concatenate_batches(batches)["id"].tolist(), [1, 3])

    def test_read_numpy_batches_numeric_comparison_predicate(self):
        path = self.enterContext(temp_csv_path(
            "region,price\n"
            "a,5000\n"
            "b,15000\n"
            "c,25000\n"
        ))

        cases = [
            (csvpy.less("price", "15000"), ["a"]),
            (csvpy.less_equal("price", "15000"), ["a", "b"]),
            (csvpy.greater("price", "15000"), ["c"]),
            (csvpy.greater_equal("price", "15000"), ["b", "c"]),
        ]
        for predicate, expected in cases:
            with self.subTest(predicate=predicate):
                batches = list(csvpy.read_numpy_batches(
                    path,
                    columns=["region"],
                    predicate=predicate,
                    batch_size=1,
                ))

                self.assertEqual(concatenate_batches(batches)["region"].tolist(), expected)

    def test_numeric_predicates_accept_python_scalar_values(self):
        path = self.enterContext(temp_csv_path(
            "region,price\n"
            "a,5000\n"
            "b,15000\n"
            "c,25000\n"
        ))

        cases = [
            (csvpy.less, "15000", ["a"]),
            (csvpy.less, 15000, ["a"]),
            (csvpy.less, 15000.0, ["a"]),
            (csvpy.less_equal, "15000", ["a", "b"]),
            (csvpy.less_equal, 15000, ["a", "b"]),
            (csvpy.less_equal, 15000.0, ["a", "b"]),
            (csvpy.greater, "15000", ["c"]),
            (csvpy.greater, 15000, ["c"]),
            (csvpy.greater, 15000.0, ["c"]),
            (csvpy.greater_equal, "15000", ["b", "c"]),
            (csvpy.greater_equal, 15000, ["b", "c"]),
            (csvpy.greater_equal, 15000.0, ["b", "c"]),
        ]
        for factory, value, expected in cases:
            with self.subTest(factory=factory.__name__, value=value):
                arrays = csvpy.read_numpy(path, columns=["region"], predicate=factory("price", value))

                self.assertEqual(arrays["region"].tolist(), expected)

    def test_predicate_factory_scalar_normalization_and_errors(self):
        self.assertEqual(csvpy.equal("id", "7").value, "7")
        self.assertEqual(csvpy.equal("id", 7).value, "7")
        self.assertEqual(csvpy.equal("ratio", 7.5).value, "7.5")
        self.assertEqual(csvpy.less("price", 15000).value, "15000")
        self.assertEqual(csvpy.greater_equal("price", 15000.5).value, "15000.5")

        factories = [
            csvpy.equal,
            csvpy.less,
            csvpy.less_equal,
            csvpy.greater,
            csvpy.greater_equal,
        ]
        invalid_values = [None, True, object(), ["15000"]]
        for factory in factories:
            for value in invalid_values:
                with self.subTest(factory=factory.__name__, value=repr(value)):
                    with self.assertRaisesRegex(TypeError, "predicate value"):
                        factory("price", value)

        with self.assertRaisesRegex(RuntimeError, "numeric predicate value is not numeric"):
            csvpy.less("price", "not numeric")

    def test_read_numpy_batches_batch_schema_infers_each_batch(self):
        path = self.enterContext(temp_csv_path(
            "mixed\n"
            "1\n"
            "2.5\n"
            "plain text\n"
        ))

        batches = list(csvpy.read_numpy_batches(path, batch_size=1, schema="batch"))

        self.assertEqual(batches[0]["mixed"].dtype, np.dtype("int64"))
        self.assertEqual(batches[0]["mixed"].tolist(), [1])
        self.assertEqual(batches[1]["mixed"].dtype, np.dtype("float64"))
        self.assertEqual(batches[1]["mixed"].tolist(), [2.5])
        self.assertIsInstance(batches[2]["mixed"].dtype, np.dtypes.StringDType)
        self.assertEqual(batches[2]["mixed"].tolist(), ["plain text"])

    def test_read_numpy_batches_sample_schema_avoids_full_prescan(self):
        path = self.enterContext(temp_csv_path(
            "mixed\n"
            "1\n"
            "plain text\n"
        ))

        sample_first = next(csvpy.read_numpy_batches(path, batch_size=1, schema="sample"))
        global_first = next(csvpy.read_numpy_batches(path, batch_size=1, schema="global"))

        self.assertEqual(sample_first["mixed"].dtype, np.dtype("int64"))
        self.assertEqual(sample_first["mixed"].tolist(), [1])
        self.assertIsInstance(global_first["mixed"].dtype, np.dtypes.StringDType)
        self.assertEqual(global_first["mixed"].tolist(), ["1"])

    def test_read_numpy_batches_cast_false_returns_strings_with_schema_option(self):
        path = self.enterContext(temp_csv_path(
            "id,flag\n"
            "1,true\n"
            "2,false\n"
        ))

        batches = list(csvpy.read_numpy_batches(path, cast=False, batch_size=1, schema="global"))
        arrays = concatenate_batches(batches)

        self.assertIsInstance(arrays["id"].dtype, np.dtypes.StringDType)
        self.assertIsInstance(arrays["flag"].dtype, np.dtypes.StringDType)
        self.assertEqual(arrays["id"].tolist(), ["1", "2"])
        self.assertEqual(arrays["flag"].tolist(), ["true", "false"])

    def test_read_numpy_batches_invalid_schema_raises_clear_error(self):
        path = self.enterContext(temp_csv_path("id\n1\n"))

        with self.assertRaisesRegex(ValueError, "schema must be 'sample', 'batch', or 'global'"):
            list(csvpy.read_numpy_batches(path, schema="wide"))

    def test_read_numpy_batches_empty_result_emits_schema_batch(self):
        path = self.enterContext(temp_csv_path(
            "region,id,value\n"
            "a,1,10\n"
            "b,2,20\n"
        ))

        expected = csvpy.read_numpy(
            path,
            columns=["id", "value"],
            predicate=csvpy.equal("region", "missing"),
        )
        batches = list(csvpy.read_numpy_batches(
            path,
            columns=["id", "value"],
            predicate=csvpy.equal("region", "missing"),
            batch_size=1,
        ))

        self.assertEqual(len(batches), 1)
        self.assertEqual(list(batches[0]), ["id", "value"])
        for column in expected:
            self.assertEqual(batches[0][column].dtype, expected[column].dtype)
            self.assertEqual(batches[0][column].tolist(), expected[column].tolist())

    def test_read_numpy_batches_handles_escaped_quoted_strings(self):
        path = self.enterContext(temp_csv_path(
            "id,text\n"
            "1,\"a,b\"\n"
            "2,\"c\"\"d\"\n"
            "3,\"line one\"\n"
        ))

        batches = list(csvpy.read_numpy_batches(path, columns=["text"], batch_size=1))

        self.assertEqual(concatenate_batches(batches)["text"].tolist(), ["a,b", 'c"d', "line one"])

    def test_read_numpy_batches_concatenate_matches_read_numpy(self):
        path = self.enterContext(temp_csv_path(
            "group,mixed,enabled,score\n"
            "keep,001,true,1\n"
            "drop,2.5,false,\n"
            "keep,plain text,true,3.5\n"
            "keep,004,,4\n"
        ))

        expected = csvpy.read_numpy(
            path,
            columns=["mixed", "enabled", "score"],
            predicate=csvpy.equal("group", "keep"),
        )
        batches = list(csvpy.read_numpy_batches(
            path,
            columns=["mixed", "enabled", "score"],
            predicate=csvpy.equal("group", "keep"),
            batch_size=2,
            schema="global",
        ))
        actual = concatenate_batches(batches)

        self.assertEqual(list(actual), list(expected))
        for column in expected:
            self.assertEqual(actual[column].dtype, expected[column].dtype)
            np.testing.assert_array_equal(actual[column], expected[column])


if __name__ == "__main__":
    unittest.main()
