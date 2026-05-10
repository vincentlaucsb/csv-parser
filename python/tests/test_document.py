import io
import unittest

import csvpy

try:
    import numpy as np
except Exception:  # pragma: no cover - depends on optional environment
    np = None


class CSVDocumentTests(unittest.TestCase):
    def test_delete_marks_rows_and_blocks_new_row_handles(self):
        document = csvpy.CSVDocument(io.StringIO("id,name\n1,alpha\n2,beta\n3,gamma\n"))

        for row in document:
            if row["id"] == "2":
                self.assertTrue(row.delete())

        self.assertTrue(document.pending_deletes)
        self.assertEqual(len(document), 2)
        with self.assertRaisesRegex(RuntimeError, "pending row deletions"):
            list(document)
        with self.assertRaisesRegex(RuntimeError, "pending row deletions"):
            document[0]

    def test_materialize_deletes_makes_iteration_safe_again(self):
        document = csvpy.CSVDocument(io.StringIO("id,name\n1,alpha\n2,beta\n3,gamma\n"))

        for row in document:
            if row["id"] == "2":
                row.delete()

        self.assertEqual(document.materialize_deletes(), 1)
        self.assertFalse(document.pending_deletes)
        self.assertEqual([row.as_list() for row in document], [["1", "alpha"], ["3", "gamma"]])

    def test_discard_deletes_restores_deleted_rows(self):
        document = csvpy.CSVDocument(io.StringIO("id,name\n1,alpha\n2,beta\n"))
        row = next(iter(document))
        row.delete()

        self.assertEqual(document.discard_deletes(), 1)
        self.assertFalse(document.pending_deletes)
        self.assertEqual([row.as_list() for row in document], [["1", "alpha"], ["2", "beta"]])

    @unittest.skipIf(np is None, "NumPy is required for CSVDocument.to_numpy tests")
    def test_to_numpy_excludes_deleted_rows_without_materializing(self):
        document = csvpy.CSVDocument(io.StringIO("id,name\n1,alpha\n2,beta\n3,gamma\n"))

        for row in document:
            if row["id"] == "2":
                row.delete()

        arrays = document.to_numpy(columns=["id", "name"], cast=False)

        self.assertTrue(document.pending_deletes)
        self.assertIsInstance(arrays["id"].dtype, np.dtypes.StringDType)
        self.assertEqual(arrays["id"].tolist(), ["1", "3"])
        self.assertEqual(arrays["name"].tolist(), ["alpha", "gamma"])

    @unittest.skipIf(np is None, "NumPy is required for CSVDocument.to_numpy tests")
    def test_to_numpy_default_casts_arrays(self):
        document = csvpy.CSVDocument(io.StringIO("id,score\n1,2.5\n2,3.5\n"))
        for row in document:
            if row["id"] == "2":
                row.delete()

        arrays = document.to_numpy()

        self.assertEqual(arrays["id"].dtype, np.dtype("int64"))
        self.assertEqual(arrays["score"].dtype, np.dtype("float64"))
        self.assertEqual(arrays["id"].tolist(), [1])
        self.assertEqual(arrays["score"].tolist(), [2.5])

    @unittest.skipIf(np is None, "NumPy is required for CSVDocument.to_numpy tests")
    def test_to_numpy_native_predicate_filters_rows(self):
        document = csvpy.CSVDocument(io.StringIO(
            "region,id,value\n"
            "el paso,1,10\n"
            "phoenix,2,20\n"
            "EL PASO,3,30\n"
        ))

        arrays = document.to_numpy(
            columns=["id", "value"],
            predicate=csvpy.equal("region", "el paso", case_sensitive=False),
        )

        self.assertEqual(arrays["id"].tolist(), [1, 3])
        self.assertEqual(arrays["value"].tolist(), [10, 30])

    def test_delete_where_marks_matching_rows(self):
        document = csvpy.CSVDocument(io.StringIO(
            "region,id\n"
            "el paso,1\n"
            "phoenix,2\n"
            "EL PASO,3\n"
        ))

        self.assertEqual(document.delete_where(csvpy.equal("region", "el paso", case_sensitive=False)), 2)
        self.assertTrue(document.pending_deletes)
        document.materialize_deletes()
        self.assertEqual([row.as_list() for row in document], [["phoenix", "2"]])

    @unittest.skipIf(np is None, "NumPy is required for CSVDocument.to_numpy tests")
    def test_quoted_escaped_fields_materialize_correctly(self):
        document = csvpy.CSVDocument(io.StringIO('id,text\n1,"a,b"\n2,"c""d"\n3,drop\n'))

        for row in document:
            if row["id"] == "3":
                row.delete()

        arrays = document.to_numpy(columns=["text"], cast=False)
        self.assertEqual(arrays["text"].tolist(), ["a,b", 'c"d'])

        document.materialize_deletes()
        self.assertEqual([row.as_list() for row in document], [["1", "a,b"], ["2", 'c"d']])


if __name__ == "__main__":
    unittest.main()
