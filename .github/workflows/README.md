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
