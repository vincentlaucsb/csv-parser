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


if __name__ == "__main__":
    unittest.main()
