#!/usr/bin/env bash
set -euo pipefail

# Run against the exact distribution bytes, without linking the CSV library.
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
generated="$(realpath "${1:?generated header directory required}")"
work="$(realpath -m "${2:-$root/build/generated-header-tests}")"
mkdir -p "$work"
cd "$root"

for compiler in g++-14 clang++-20; do
  for standard in 11 20 23; do
    for exceptions in 0 1; do
      for threads in 0 1; do
        flags=(-DCSV_TEST_EXCEPTIONS="$exceptions")
        if [ "$exceptions" = 0 ]; then flags+=(-fno-exceptions); fi
        if [ "$standard" = 23 ]; then flags+=(-DCSV_TEST_EXPECT_EXPECTED=1); fi
        binary="$work/${compiler}-cpp${standard}-exceptions${exceptions}-threads${threads}"
        "$compiler" -std=c++"$standard" -O2 -pthread \
          -DCSV_ENABLE_THREADS="$threads" -DCSV_TEST_CXX_STANDARD="$standard" \
          -I "$generated" "${flags[@]}" \
          tests/compatibility/explicit_errors/smoke.cpp \
          tests/compatibility/explicit_errors/second.cpp \
          tests/compatibility/explicit_errors/error_storage.cpp -o "$binary"
        "$binary"
        echo "PASS: $compiler C++$standard exceptions=$exceptions threads=$threads"
      done
    done
  done
done

# The Catch2 cases include the existing >=500K-row mmap/stream regressions.
# Run suites sequentially because they share temporary fixture names.
for threads in ON OFF; do
  for exceptions in ON OFF; do
    flags=()
    if [ "$exceptions" = OFF ]; then flags=(-DCMAKE_CXX_FLAGS=-fno-exceptions); fi
    profile="$work/regressions-threads$threads-exceptions$exceptions"
    cmake -S tests/compatibility/generated_header -B "$profile" \
      -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14 \
      -DCSV_SOURCE_ROOT="$root" -DCSV_GENERATED_INCLUDE_DIR="$generated" \
      -DCSV_ENABLE_THREADS="$threads" -DCSV_TEST_EXCEPTIONS="$exceptions" \
      -DFETCHCONTENT_BASE_DIR="$work/deps-exceptions$exceptions" "${flags[@]}"
    cmake --build "$profile" --parallel 2
    ctest --test-dir "$profile" --output-on-failure
  done
done
