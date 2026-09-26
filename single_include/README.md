# Single Header Distribution

> **`single_include/csv.hpp` is now a small compatibility shim, not the full amalgamated header.**

## For Users

**[📥 Download csv.hpp](https://vincentlaucsb.github.io/csv-parser/csv.hpp)** — Available on GitHub Pages

Or copy the URL:
```
https://vincentlaucsb.github.io/csv-parser/csv.hpp
```

The Pages copy is regenerated on pushes to `master` and manual runs of the
documentation workflow. Tagged releases also attach a generated `csv.hpp` and
its SHA-256 checksum as release assets.

### Usage

Once downloaded, simply include it in your project:

```cpp
#include "csv.hpp"

// Use the library as normal
```

No build configuration needed — everything is self-contained in the single file.

---

## For Maintainers

### Generating the Single Header Locally

To generate the amalgamated header yourself:

```bash
single_header --config single_header.json --output output_path/csv.hpp
```

This reads the generation settings from `single_header.json`, pulls source files from `include/`, and produces a completely self-contained header file.

### Validation

The build generates the header under `build/single_include_generated/` when
`generate_single_header` or its dependent `single_include_test` target runs.
CI builds the multi-translation-unit smoke test on Linux and macOS; its Windows
job currently skips that target. The documentation workflow generates a
separate copy for GitHub Pages.

Run the smoke test locally:

```bash
cmake -S . -B build -DCSV_BUILD_SINGLE_INCLUDE_TEST=ON -DCSV_SINGLE_HEADER_EXECUTABLE=/path/to/single_header
cmake --build build --target single_include_test
```

### Important Notes

- **Do not compile or replace `single_include/csv.hpp`** — the tracked file is an intentional compatibility shim
- **Do not commit the generated distribution header** — edit the source files in `include/` instead
- **The build-generated header is under `build/single_include_generated/`** — this is the artifact used by `single_include_test`
- **GitHub Pages serves the current generated version** — see [For Users](#for-users) above

### Distribution Model

- **Build-time:** CMake generates into build tree for testing
- **Documentation workflow:** Publishes the generated header to GitHub Pages after pushes to `master`
- **Tag release workflow:** Attaches the generated header and checksum to the GitHub release
- **Repository:** Source files and the tracked compatibility shim; no pre-generated distribution header

This model ensures:
- No stale artifacts in git
- A Pages copy regenerated after pushes to `master`
- Deterministic, reproducible generation
- Clean maintenance burden
