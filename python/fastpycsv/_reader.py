"""Lazy stdlib-adjacent reader facade over the native fastpycsv extension."""

from __future__ import annotations

import bz2
import gzip
import lzma
import os
import shutil
import tempfile
import zipfile
from pathlib import PurePosixPath
from typing import Iterable, Optional, Sequence

from ._format import _make_format
from .fastpycsv import _RowsReader
from .fastpycsv import _rows_reader_from_stream
from .fastpycsv import _rows_reader_from_zip
from .fastpycsv import _zip_members as _native_zip_members

_CSV_LIKE_SUFFIXES = frozenset({".csv", ".tsv", ".txt"})
try:
    import compression.zstd as _zstd
except Exception:  # pragma: no cover - Python < 3.14 or zstd unavailable
    _zstd = None

_COMPRESSED_OPENERS = {
    ".gz": gzip.open,
    ".bz2": bz2.open,
    ".xz": lzma.open,
    ".lzma": lzma.open,
}
if _zstd is not None:
    _COMPRESSED_OPENERS[".zst"] = _zstd.open
    _COMPRESSED_OPENERS[".zstd"] = _zstd.open


def _is_path(value: object) -> bool:
    return isinstance(value, (str, bytes, os.PathLike))


def _at_start(value: object) -> bool:
    try:
        return value.tell() == 0
    except (AttributeError, OSError):
        return False


class _TempCSV:
    def __init__(self, rows: Iterable[str]):
        handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="", delete=False)
        self.name = handle.name
        with handle:
            for row in rows:
                handle.write(row)

    def cleanup(self) -> None:
        try:
            os.unlink(self.name)
        except FileNotFoundError:
            pass


class _TempPath:
    def __init__(self):
        handle = tempfile.NamedTemporaryFile("wb", delete=False)
        self.name = handle.name
        handle.close()

    def cleanup(self) -> None:
        try:
            os.unlink(self.name)
        except FileNotFoundError:
            pass


def _is_zip_path(path: str) -> bool:
    return os.fspath(path).lower().endswith(".zip")


def _compressed_opener(path: str):
    return _COMPRESSED_OPENERS.get(PurePosixPath(os.fspath(path).lower()).suffix)


def _is_csv_like_member(name: str) -> bool:
    if not name or name.endswith("/"):
        return False
    return PurePosixPath(name).suffix.lower() in _CSV_LIKE_SUFFIXES


def zip_members(path) -> list[str]:
    """Return non-directory member names from a ZIP archive."""
    return list(_native_zip_members(os.fspath(path)))


def _select_zip_member(path: str, requested: Optional[str]) -> str:
    members = zip_members(path)
    if requested is not None:
        if requested not in members:
            raise ValueError(f"ZIP member not found: {requested}")
        if PurePosixPath(requested).suffix.lower() == ".zip":
            raise ValueError("recursive ZIP members are not supported")
        return requested

    candidates = [name for name in members if _is_csv_like_member(name)]
    if not candidates:
        raise ValueError("ZIP archive contains no CSV-like members")
    if len(candidates) > 1:
        names = ", ".join(candidates)
        raise ValueError(f"ZIP archive contains multiple CSV-like members; specify member=. Candidates: {names}")
    return candidates[0]


def _extract_zip_member(path: str, member: Optional[str]) -> _TempPath:
    selected = _select_zip_member(path, member)
    temp = _TempPath()
    try:
        with zipfile.ZipFile(path) as archive:
            with archive.open(selected) as source, open(temp.name, "wb") as target:
                shutil.copyfileobj(source, target, length=1024 * 1024)
    except Exception:
        temp.cleanup()
        raise
    return temp


def _extract_compressed_path(path: str) -> _TempPath:
    opener = _compressed_opener(path)
    if opener is None:
        raise ValueError(f"unsupported compressed input: {path}")
    temp = _TempPath()
    try:
        with opener(path, "rb") as source, open(temp.name, "wb") as target:
            shutil.copyfileobj(source, target, length=1024 * 1024)
    except (EOFError, OSError, lzma.LZMAError) as exc:
        temp.cleanup()
        raise ValueError(f"failed to decompress input: {path}") from exc
    return temp


class _CSVSource:
    def __init__(self, csvfile, member: Optional[str] = None, prefer_stream: bool = False):
        self._temp = None
        self.stream = None
        self._stream_factory = None
        self.zip_path = None
        self.zip_member = None
        self.name = self._filename(csvfile, member, prefer_stream)

    @property
    def is_stream(self) -> bool:
        return self._stream_factory is not None

    @property
    def is_zip_stream(self) -> bool:
        return self.zip_path is not None

    def open_stream(self):
        if self.stream is None:
            self.stream = self._stream_factory()
        return self.stream

    def cleanup(self) -> None:
        if self.stream is not None:
            close = getattr(self.stream, "close", None)
            if close is not None:
                close()
            self.stream = None
        if self._temp is not None:
            self._temp.cleanup()

    def _filename(self, csvfile, member: Optional[str], prefer_stream: bool) -> Optional[str]:
        if _is_path(csvfile):
            path = os.fspath(csvfile)
            if _is_zip_path(path):
                selected = _select_zip_member(path, member)
                if prefer_stream:
                    self.zip_path = path
                    self.zip_member = selected
                    return None
                self._temp = _extract_zip_member(path, selected)
                return self._temp.name
            if _compressed_opener(path) is not None:
                if member is not None:
                    raise ValueError("member= is only supported for ZIP inputs")
                if prefer_stream:
                    opener = _compressed_opener(path)
                    self._stream_factory = lambda path=path, opener=opener: opener(path, "rb")
                    return None
                self._temp = _extract_compressed_path(path)
                return self._temp.name
            if member is not None:
                raise ValueError("member= is only supported for ZIP inputs")
            return path

        if (
            hasattr(csvfile, "name")
            and _is_path(csvfile.name)
            and not getattr(csvfile, "closed", False)
            and _at_start(csvfile)
        ):
            path = os.fspath(csvfile.name)
            if _is_zip_path(path):
                selected = _select_zip_member(path, member)
                if prefer_stream:
                    self.zip_path = path
                    self.zip_member = selected
                    return None
                self._temp = _extract_zip_member(path, selected)
                return self._temp.name
            if _compressed_opener(path) is not None:
                if member is not None:
                    raise ValueError("member= is only supported for ZIP inputs")
                if prefer_stream:
                    opener = _compressed_opener(path)
                    self._stream_factory = lambda path=path, opener=opener: opener(path, "rb")
                    return None
                self._temp = _extract_compressed_path(path)
                return self._temp.name
            if member is not None:
                raise ValueError("member= is only supported for ZIP inputs")
            return path

        if member is not None:
            raise ValueError("member= is only supported for ZIP path inputs")
        self._temp = _TempCSV(csvfile)
        return self._temp.name


class _MaterializedRows:
    def __init__(self, parent: "_BaseReader", iterator):
        self._parent = parent
        self._iterator = iterator

    def __iter__(self):
        return self

    def __next__(self):
        try:
            return next(self._iterator)
        except StopIteration:
            self._parent._source.cleanup()
            raise

    def all(self):
        try:
            return self._iterator.all()
        finally:
            self._parent._source.cleanup()

    def chunks(self, size: int):
        if size <= 0:
            raise ValueError("chunk size must be greater than zero")

        def chunk_iterator():
            while True:
                batch = self._iterator.read_chunk(size)
                if not batch:
                    self._parent._source.cleanup()
                    return
                yield batch

        return chunk_iterator()


class _BaseReader:
    def _init_reader(self, csvfile, fmt, cast: bool, batch_size: int, member: Optional[str]) -> None:
        self._source = _CSVSource(csvfile, member, prefer_stream=True)
        self._fmt = fmt
        self._cast = cast
        self._batch_size = batch_size
        self._iterator = None
        try:
            if self._source.is_stream or self._source.is_zip_stream:
                return
            else:
                self._iterator = iter(_RowsReader(self._source.name, fmt, cast, batch_size))
        except Exception:
            self._source.cleanup()
            raise

    def _ensure_iterator(self):
        if self._iterator is not None:
            return self._iterator

        try:
            if self._source.is_zip_stream:
                self._iterator = iter(_rows_reader_from_zip(
                    self._source.zip_path,
                    self._source.zip_member,
                    self._fmt,
                    self._cast,
                    self._batch_size,
                ))
            else:
                self._iterator = iter(_rows_reader_from_stream(
                    self._source.open_stream(),
                    self._fmt,
                    self._cast,
                    self._batch_size,
                ))
        except Exception:
            self._source.cleanup()
            raise

        return self._iterator

    @property
    def fieldnames(self):
        return list(self._ensure_iterator().fieldnames)

    def get_col_names(self):
        return self.fieldnames

    def __iter__(self):
        return self

    def __next__(self):
        try:
            row = next(self._ensure_iterator())
        except StopIteration:
            self._source.cleanup()
            raise
        return row

    def filter(self, predicate, *, append: bool = True):
        if predicate is None:
            raise TypeError("reader.filter() expects a fastpycsv predicate; create a fresh reader for an unfiltered pass")
        self._ensure_iterator().filter(predicate, append)
        return self

    def lists(self, columns=None):
        return _MaterializedRows(self, self._ensure_iterator().lists(columns))

    def tuples(self, columns=None):
        return _MaterializedRows(self, self._ensure_iterator().tuples(columns))

    def dicts(self, columns=None):
        return _MaterializedRows(self, self._ensure_iterator().dicts(columns))

    def to_lists(self, columns=None):
        return self.lists(columns).all()

    def to_tuples(self, columns=None):
        return self.tuples(columns).all()

    def to_dicts(self, columns=None):
        return self.dicts(columns).all()

    def __del__(self):
        source = getattr(self, "_source", None)
        if source is not None:
            source.cleanup()


class _Reader(_BaseReader):
    def __init__(
        self,
        csvfile,
        *,
        delimiter: str = ",",
        quotechar: Optional[str] = '"',
        doublequote: bool = True,
        skipinitialspace: bool = False,
        strict: bool = False,
        cast: bool = False,
        typed: Optional[bool] = None,
        consume_header: bool = True,
        fieldnames: Optional[Sequence[str]] = None,
        batch_size: int = 8192,
        member: Optional[str] = None,
    ):
        if typed is not None:
            cast = typed
        fmt = _make_format(
            delimiter,
            quotechar,
            doublequote,
            skipinitialspace,
            strict,
            fieldnames=fieldnames,
            no_header=(not consume_header) or fieldnames is not None,
        )
        self._init_reader(csvfile, fmt, cast, batch_size, member)


def reader(csvfile, dialect="excel", **fmtparams) -> _Reader:
    if dialect != "excel":
        raise NotImplementedError("fastpycsv.reader currently supports only the default excel dialect")
    return _Reader(csvfile, **fmtparams)
