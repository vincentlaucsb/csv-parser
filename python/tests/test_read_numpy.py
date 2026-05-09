import math
import os
import tempfile
import unittest

import csvpy

try:
    import numpy as np
except Exception:  # pragma: no cover - depends on optional environment
    np = None

try:
    import pandas as pd
except Exception:  # pragma: no cover - depends on optional environment
    pd = None


def _write_temp_csv(text):
    handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="", delete=False)
    with handle:
        handle.write(text)
    return handle.name


@unittest.skipIf(np is None, "NumPy is required for csvpy.read_numpy tests")
class ReadNumpyTests(unittest.TestCase):
    def test_read_numpy_materializes_typed_arrays(self):
        path = _write_temp_csv(
            "name,count,ratio,flag\n"
            "alpha,1,1.5,true\n"
            "beta,2,2.25,false\n"
        )
        self.addCleanup(lambda: os.path.exists(path) and os.unlink(path))

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
        path = _write_temp_csv(
            "id,score,enabled\n"
            "1,2.5,true\n"
            ",,false\n"
            "3,4.5,\n"
        )
        self.addCleanup(lambda: os.path.exists(path) and os.unlink(path))

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
        path = _write_temp_csv(
            "id,text,skip\n"
            "1,\"a,b\",x\n"
            "2,\"c\"\"d\",y\n"
        )
        self.addCleanup(lambda: os.path.exists(path) and os.unlink(path))

        arrays = csvpy.read_numpy(path, columns=["text", "id"])

        self.assertEqual(list(arrays), ["text", "id"])
        self.assertEqual(arrays["text"].tolist(), ["a,b", 'c"d'])
        self.assertEqual(arrays["id"].tolist(), [1, 2])

    @unittest.skipIf(pd is None, "pandas is required for DataFrame handoff test")
    def test_read_numpy_result_builds_pandas_dataframe(self):
        path = _write_temp_csv(
            "name,value\n"
            "alpha,1\n"
            "beta,2\n"
        )
        self.addCleanup(lambda: os.path.exists(path) and os.unlink(path))

        frame = pd.DataFrame(csvpy.read_numpy(path))

        self.assertEqual(list(frame.columns), ["name", "value"])
        self.assertEqual(frame["name"].tolist(), ["alpha", "beta"])
        self.assertEqual(frame["value"].tolist(), [1, 2])


if __name__ == "__main__":
    unittest.main()
