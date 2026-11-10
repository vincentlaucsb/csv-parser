# CSV parser fuzzing

Fuzzers are opt-in (`CSV_BUILD_FUZZERS=OFF` by default). Native Unix Clang,
libFuzzer, C++20 and thread support are required. They compile a separate parser
library with sanitizer and coverage instrumentation, including the `.cpp` files;
ordinary library targets and downstream compile flags are unchanged. The harnesses
include canonical `include/csv.hpp`, never the compatibility shim.

```sh
cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
  -DCSV_BUILD_FUZZERS=ON -DCSV_BUILD_TESTS=OFF -DCSV_BUILD_PROGRAMS=OFF
cmake --build build/fuzz --target csv_fuzz_stream csv_fuzz_differential csv_fuzz_boundary
mkdir -p build/corpus build/crashes
cp fuzz/corpus/differential/* build/corpus/
build/fuzz/fuzz/csv_fuzz_differential build/corpus -dict=fuzz/csv.dict \
  -max_len=65536 -max_total_time=60 -timeout=90 -rss_limit_mb=2048 \
  -artifact_prefix=build/crashes/
```

Executable paths include the `fuzz/` build subdirectory. Replay a crash or seed by
passing its filename instead of a corpus directory. Minimize with libFuzzer's
`-minimize_crash=1 -exact_artifact_path=minimized INPUT`; retain the reproducer in
the corresponding committed `fuzz/corpus/` directory. These corpora are tracked
here rather than in the independently versioned `tests/data` submodule.

`stream` checks arbitrary byte input through the checked stream APIs. Its first
byte selects delimiter or guessing, header, quoting, trimming, and variable-column
policy; remaining bytes are CSV (including NULs and malformed encodings).
`differential` uses that same encoding and compares stream versus mmap in both
synchronous and threaded scheduling, yielding periodically in the consumer. It
copies every field into owned strings and compares rows, column names, resolved
format, row count and terminal error code. Backend-specific diagnostic text and
system errors are intentionally excluded. At terminal status, a move construction joins the worker before the final checked
error read; reader destruction also joins before the temporary file is rewritten.
One process-unique file is reused across iterations and owned by `FileGuard`.

`boundary` consumes up to 64 recipe bytes: byte 0 chooses an offset from -8 to +8
around a read-window boundary; byte 1 chooses closing/escaped quotes, embedded
newline, CRLF, EOF, unterminated quote, extra delimiter, or NUL; byte 2 bit 0
selects the minimum supported read window instead of the default 10MB window.
Byte 2 bit 1 targets mmap's window after its 500000-byte preview and row rewind;
otherwise the recipe targets the stream's first window. Boundary recipes use one
parser worker so speculative window expansion cannot move the boundary.
Default-window recipes expand to over 500K rows with distinct column values;
minimum-window recipes provide faster mutation. Use `-max_len=64` for this target.
The maximum synthesis is about 11MB, avoiding giant checked-in seeds. Differential
snapshots deliberately retain owned values, so allow 2GB RSS for sanitizer runs.
Harnesses expose only `LLVMFuzzerTestOneInput`, allowing other compatible engines.

The dedicated workflow replays committed seeds on every PR and master push,
fuzzes both fast targets for 60 seconds, and runs each target for 15 minutes on
nightly/manual runs. Boundary recipes are replayed on PRs too. Inputs, logs and
crashes are uploaded even on failure; successful trusted runs save their evolving
corpus in an Actions cache. Each target has independent timeout/RSS limits and a
job deadline. Cache eviction is harmless because committed regressions are always
replayed. Use a separate build with `-DCSV_FUZZ_SANITIZER=thread` for deterministic
TSan replay (no libFuzzer runtime and no ASan combination):

```sh
build/tsan/fuzz/csv_fuzz_differential fuzz/corpus/differential/*
build/tsan/fuzz/csv_fuzz_boundary fuzz/corpus/boundary/*
```

TSan exercises the same thread/synchronous differential matrix using a standalone
replay driver. It accepts explicit filenames and exits nonzero on missing inputs.
Sanitizer findings and equality violations terminate the process; do not catch and
ignore them. Allocation failures are also findings within the configured resource
budget. Thread scheduling is intentionally perturbed, so replay is deterministic
in input selection and order, not OS scheduling.
