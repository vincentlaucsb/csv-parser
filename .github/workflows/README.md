# Safety Checks

The repository runs runtime sanitizers and CodeQL on pushes and pull requests
targeting `master`.

| Workflow | Coverage |
| --- | --- |
| [`sanitizers.yml`](sanitizers.yml) | Linux ASan (C++11 and C++20), TSan (C++20), and UBSan (C++17); Windows/MSVC ASan (C++20). Each job builds and runs CTest. |
| [`codeql.yml`](codeql.yml) | C++ security and quality queries on pushes, pull requests, and a weekly schedule. Results appear in GitHub's code scanning view. |

The sanitizer jobs upload test logs when they fail. TSan can expose races in
the reader's worker and queue paths; ASan and UBSan cover memory and undefined
behavior errors. These are separate jobs because their instrumentation has
different runtime requirements.

## Local sanitizer run

On Linux, configure a separate build directory for each sanitizer. For example:

```sh
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug \
  -DCSV_CXX_STANDARD=20 \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g"
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure
```

Substitute `-fsanitize=thread` or `-fsanitize=undefined` in both flag values
to reproduce the corresponding Linux job. For Windows/MSVC ASan, use the
configuration in [`sanitizers.yml`](sanitizers.yml); it also sets linker flags
needed by that job.

## Optimized single-header distribution

[`optimized-single-header.yml`](optimized-single-header.yml), documentation, and
release publication share [one generation action](../actions/optimized-single-header/action.yml).
It builds `single_header` **1.1.0** with the LLVM/Clang 20 backend, then runs
`tools/generate_optimized_header.py`. The script selects exactly one compilation
command per CSV implementation source, expands it into native/Emscripten-branch
and compiler-exceptions-on/off contexts, and invokes CMake's `generate_single_header`
target with `CSV_SINGLE_HEADER_EXTRA_ARGS`. The canonical generated file is
`build/optimized-header/analysis/single_include_generated/csv.hpp`.

All passes are explicit: automatic `inline` insertion, free-function/method stitching, include hoisting and
deduplication, recursive namespace merging, and automatic output cleanup. The
script requires a valid compiler baseline, accepted free functions and methods,
fresh final validation in all 36 source contexts, one occurrence each of
`<algorithm>`, `<vector>`, and `<string>`, and fewer named namespace openings.
The namespace count is a size metric; runtime tests validate the resulting artifact.
An independent stitching-OFF pass verifies that residual definitions also receive
`inline`. The source files use ordinary compiled-library definitions; the generator
owns distribution linkage. Lightweight generator binaries reject this mode rather
than emitting functions with unsafe linkage. Header templates and in-class methods
retain their existing ODR rules.
The CMake generator command always requests automatic inline insertion explicitly,
so an older CLI cannot silently ignore the new JSON option and emit unsafe output.

`single_header.json` lists 21 reviewed mixed-include audit waivers. Each names a
standard header already required unconditionally elsewhere in the CSV API; its
conditional repeats occur in bundled dependencies or alternate implementations.
`<mutex>` is required unconditionally by DataFrame storage/overlay locking even
when background parsing is disabled. These waivers permit hoisting without
introducing a new feature dependency. `<thread>` has no waiver and remains
conditional on `CSV_ENABLE_THREADS`. Keep new conflicts visible rather than
automatically extending this list, and leave pinned vendored headers unchanged.

`tools/test_generated_header.sh` compiles and runs the exact generated header
with GCC 14 and Clang 20, C++11/20/23, both compiler exception modes, and both
thread modes. It also runs existing field/format/row/iterator, checked file reading,
DataFrame, writing, and large round-trip Catch2 cases against that header without
linking the CSV library. Categorical suites run in both compiler exception modes
and both thread modes, including checked 500K-row chunk-boundary cases. The suites run
sequentially because their fixtures share temporary names. Source-only and
generated consumers continue to use the same public API.

Compiler-matrix workflows call the shared generation workflow first and download
its artifact. They pass `CSV_SINGLE_HEADER_PREGENERATED_HEADER` to CMake, whose
`generate_single_header` target verifies the manifest's input-tree and header
hashes before copying those bytes into `single_include_generated/csv.hpp`.
Python is needed for this verification. The manifest covers all source headers,
implementation files, and the generation configuration; the configuration is
checked out with LF endings so hashes agree across operating systems.

To reproduce with a locally built compiler-backed generator:

```sh
python3 tools/generate_optimized_header.py --executable /path/to/single_header \
  --output build/distribution/csv.hpp
bash tools/test_generated_header.sh build/optimized-header/analysis/single_include_generated
```

The release workflow checks out the requested tag, regenerates and validates
the artifact, then uploads `csv.hpp`, its SHA-256 checksum, the stitching report,
and a manifest recording enabled passes, byte counts, namespace counts, and the
generator binary hash. Release publication triggers both header assets and the
Python publishing workflow. Documentation publishes a separately generated,
equally validated header on each `master` update.
