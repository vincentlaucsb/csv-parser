import os
import tempfile
from contextlib import contextmanager


@contextmanager
def temp_csv_path(text):
    handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="", delete=False)
    try:
        with handle:
            handle.write(text)
        yield handle.name
    finally:
        try:
            os.unlink(handle.name)
        except FileNotFoundError:
            pass
