# Type Casting

By default, `fastpycsv.reader()` returns strings. This matches Python's standard
library `csv.reader()` behavior and keeps parsing predictable.

Pass `cast=True` when you want csv-parser's scalar classification surfaced as
Python values:

```python
import fastpycsv

reader = fastpycsv.reader(["id,amount,active\n", "1,2.5,true\n"], cast=True)
rows = list(reader)

assert reader.fieldnames == ["id", "amount", "active"]
assert rows[0].as_list() == [1, 2.5, True]
```

The mapping is:

| CSV value classification | Python value |
| --- | --- |
| empty/null field | `None` |
| boolean | `bool` |
| integer | `int` |
| floating point | `float` |
| timestamp | `datetime.datetime` |
| string or mixed value | `str` |

Use casting for exploratory data work and ETL scripts where Python values are
more useful than raw strings. Keep the default string mode when exact textual
round-tripping matters.
