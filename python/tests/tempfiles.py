import os
import tempfile
import zipfile
from contextlib import contextmanager
import bz2
import gzip
import lzma

try:
    import compression.zstd as zstd
except Exception:  # pragma: no cover - Python version dependent
    zstd = None


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


@contextmanager
def temp_zip_path(members, compression=zipfile.ZIP_DEFLATED):
    handle = tempfile.NamedTemporaryFile("wb", suffix=".zip", delete=False)
    handle.close()
    try:
        with zipfile.ZipFile(handle.name, "w", compression=compression) as archive:
            for name, data in members.items():
                archive.writestr(name, data)
        yield handle.name
    finally:
        try:
            os.unlink(handle.name)
        except FileNotFoundError:
            pass


@contextmanager
def temp_compressed_csv_path(text, suffix):
    handle = tempfile.NamedTemporaryFile("wb", suffix=suffix, delete=False)
    handle.close()
    try:
        if suffix == ".gz":
            with gzip.open(handle.name, "wt", encoding="utf-8", newline="") as out:
                out.write(text)
        elif suffix == ".bz2":
            with bz2.open(handle.name, "wt", encoding="utf-8", newline="") as out:
                out.write(text)
        elif suffix in {".xz", ".lzma"}:
            with lzma.open(handle.name, "wt", encoding="utf-8", newline="") as out:
                out.write(text)
        elif suffix in {".zst", ".zstd"} and zstd is not None:
            with zstd.open(handle.name, "wt", encoding="utf-8", newline="") as out:
                out.write(text)
        else:
            raise ValueError(f"unsupported test compression suffix: {suffix}")
        yield handle.name
    finally:
        try:
            os.unlink(handle.name)
        except FileNotFoundError:
            pass
