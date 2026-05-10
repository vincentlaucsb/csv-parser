# csvpy Python Bindings

`csvpy` is the optional Python binding for Vince's CSV Parser. It is designed
for users who want a familiar Python reader facade backed by the C++ parser's
fast, ETL-focused internals.

The package is named `csvpy`; it does not replace or shadow Python's standard
library `csv` module.

```{toctree}
:maxdepth: 2

quickstart
api
type-casting
numpy
benchmarks
```

## Build These Docs

From the repository root:

```powershell
python -m pip install -r python/docs/requirements.txt
python -m sphinx -b html python/docs python/docs/_build/html
```

The generated HTML lands in `python/docs/_build/html`.

