# Quickstart

Build the Python extension by enabling `BUILD_PYTHON`:

```powershell
cmake -S . -B build/csvpy -DBUILD_PYTHON=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/csvpy --target csvpy --config Release
```

For an existing top-level build, the `csvpy` target bootstraps its own Python
build tree:

```powershell
cmake --build build/x64-Release --target csvpy --config Release
```

Use `CSVPY_BOOTSTRAP_PYTHON_EXECUTABLE` when the build should target a specific
Python interpreter:

```powershell
cmake -S . -B build/x64-Release -DCSVPY_BOOTSTRAP_PYTHON_EXECUTABLE=C:/Python314/python.exe
```

## Read Rows

`csvpy.reader()` behaves like a lightweight standard-library reader facade.
By default, it consumes the first row as column names. Rows are lazy, list-like
objects backed by the native extension.

```python
import csvpy

with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.reader(handle):
        print(row["name"])
```

Use `row.as_list()` when you want a regular Python list:

```python
rows = []
with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.reader(handle):
        rows.append(row.as_list())
```

Pass `consume_header=False` when the first input row should be emitted as data:

```python
with open("headerless.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.reader(handle, consume_header=False):
        print(row[0])
```

Pass `fieldnames=[...]` when a file has no header row but you still want
column-name indexing:

```python
import csvpy

with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.reader(handle, fieldnames=["name", "age"]):
        print(row["name"])
```

Use `row.as_dict()` only when you explicitly need a plain Python dictionary.
