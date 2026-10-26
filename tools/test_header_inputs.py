import tempfile
import unittest
from pathlib import Path

from header_inputs import source_tree_sha256


class HeaderInputTests(unittest.TestCase):
    def test_checkout_newlines_are_portable_but_content_changes_are_detected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "include").mkdir()
            source = root / "include/probe.hpp"
            config = root / "single_header.json"
            source.write_bytes(b"int answer() { return 42; }\n")
            config.write_bytes(b"{}\n")
            expected = source_tree_sha256(root)
            for path in (source, config):
                path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
            self.assertEqual(expected, source_tree_sha256(root))
            source.write_bytes(b"int answer() { return 43; }\r\n")
            self.assertNotEqual(expected, source_tree_sha256(root))


if __name__ == "__main__":
    unittest.main()
