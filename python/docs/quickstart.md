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
Rows are lazy, list-like objects backed by the native extension.

```python
import csvpy

with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.reader(handle):
        print(row[0])
```

Use `row.as_list()` when you want a regular Python list:

```python
rows = []
with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.reader(handle):
        rows.append(row.as_list())
```

## Header-Aware Rows

`csvpy.rows()` uses the first row as headers unless `fieldnames` is provided.
Rows support column-name indexing without materializing a dictionary for every
row.

```python
import csvpy

with open("data.csv", newline="", encoding="utf-8") as handle:
    for row in csvpy.rows(handle):
        print(row["name"])
```

Use `row.as_dict()` only when you explicitly need a plain Python dictionary.
