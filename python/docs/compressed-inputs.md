# Reading Compressed Files

`fastpycsv` accepts compressed file paths through source adapters. The CSV
parser sees ordinary decompressed CSV bytes, so delimiter handling, quoting,
headers, predicates, NumPy export, and materialized row iterators behave the
same as they do for normal CSV paths.

Supported path suffixes:

| Suffix | Adapter |
| --- | --- |
| `.zip` | Native zlib-ng ZIP reader |
| `.gz` | Python `gzip` |
| `.bz2` | Python `bz2` |
| `.xz`, `.lzma` | Python `lzma` |
| `.zst`, `.zstd` | Python `compression.zstd`, when available |

Compressed source support is read-only. `fastpycsv.write_csv()` does not write
compressed files.

## ZIP Archives

When a ZIP archive has exactly one CSV-like member (`.csv`, `.tsv`, or `.txt`),
`reader()` selects it automatically:

```python
for row in fastpycsv.reader("export.zip"):
    consume(row)
```

When an archive has more than one CSV-like member, select one explicitly:

```python
rows = (
    fastpycsv.reader("export.zip", member="tables/vehicles.csv")
    .dicts(["id", "price"])
    .all()
)
```

List archive members before choosing:

```python
members = fastpycsv.zip_members("export.zip")
```

The same ZIP source adapter is used by row readers, materialized iterators, and
NumPy exports:

```python
arrays = fastpycsv.read_numpy(
    "export.zip",
    member="tables/vehicles.csv",
    columns=["price", "year"],
)

for batch in fastpycsv.read_numpy_batches(
    "export.zip",
    member="tables/vehicles.csv",
    columns=["price", "year"],
):
    consume(batch)
```

Lazy row readers and materialized row iterators use fastpycsv's native zlib-ng
ZIP reader for stored and deflated members, then stream the selected member into
the native CSV parser. Encrypted members, nested ZIP archives, non-deflated ZIP
compression methods, and ZIP writing are intentionally unsupported.

## Gzip, Bzip2, XZ, LZMA, And Zstd

Single-stream compressed files use Python standard-library decompression:

```python
rows = fastpycsv.reader("vehicles.csv.gz").dicts().all()
arrays = fastpycsv.read_numpy("vehicles.csv.xz", columns=["price", "year"])
```

`.zst` and `.zstd` are available only on Python builds that provide
`compression.zstd`.

These formats do not have archive members, so `member=` is only valid for ZIP
inputs.

## Performance Notes

Compressed inputs are source adapters, not parser-core features.

For lazy row iteration and materialized row iterators, compressed inputs use the
stream parser directly:

- `.zip` paths use a native zlib-ng backed `std::istream`, so rows can flow into
  csv-parser without Python file-object callbacks.
- `.gz`, `.bz2`, `.xz`, `.lzma`, `.zst`, and `.zstd` are opened with Python
  decompression modules and bridged into `std::istream`.

These row-oriented paths do not stage the full decompressed payload in memory or
in a temporary file.

The eager NumPy APIs are still path-oriented internally. For compressed
`read_numpy()` and `read_numpy_batches()` calls, fastpycsv stages the
decompressed source to a temporary file before opening the existing native NumPy
path. This preserves current dtype inference and replay behavior, but it means
NumPy compressed reads still pay the decompression-to-disk cost.

Practical implications:

- Normal uncompressed CSV paths remain the fastest path.
- Compressed paths add decompression time before parsing begins.
- Row and materialized-row compressed reads stream and do not create a
  decompressed temporary file.
- NumPy compressed reads can use temporary disk space equal to the decompressed
  source size.
- Peak memory stays close to the existing reader or NumPy path; compressed
  payloads are not held as one large Python object.
- For repeated reads of the same large compressed file, manually decompressing
  once and reading the resulting CSV path may still be faster, especially for
  NumPy workflows.

Temporary files used by NumPy compressed reads are owned by the NumPy iterator
or eager read call and are cleaned up when the read is exhausted, an eager NumPy
read completes, or the object is garbage-collected.
