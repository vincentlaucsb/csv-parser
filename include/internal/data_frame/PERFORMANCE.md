# DataFrameColumn performance investigation — 2026-10-10

The direct string-view extraction investigated here is retained in the DataFrame implementation. Physical-column caching was investigated separately and rejected. Both experiments used baseline commit `030bbed158dcf0b8226dfdbe294877f3feb8fe18`. The results below record the isolated prototypes, before integration.

## Change

Add an internal `csv::internals::data_frame::RowViewAccessor`, forward-declared and friended by CSVRow. Its inline accessor calls CSVRow's existing `get_field_impl(column, row.data)`. CleanDataFrame::view and DirtyDataFrame::view's unedited fallback call it instead of constructing CSVField through CSVRow::operator[]. Sparse overlay retrieval and its locking remain untouched. There are no new public methods, objects, caches, member fields, virtuals, or state checks. CSVRow layout unchanged; DataFrameColumn remains 48 bytes.

This reuses the canonical decoding, realized quote storage, trimming, and column bounds checks. The backend still validates visible column bounds and row bounds. It removes wrapper construction and scalar metadata work and exposes the existing inline field extraction to the optimizer. It does not change the actual DataFrameColumn cell-proxy iterator path.

## Method

Windows Intel Core i5-12400 (6 cores/12 logical), MSVC `/O2 /MD /EHsc /std:c++20`. Identical retained source [benchmark source](../../../tools/benchmarks/data_frame/benchmark.cpp) for all builds. Multiheader variants link identical existing release csv.lib; header-only variants generated from copied baseline/candidate include trees through `generate_single_header` target using canonical single_header.json and generator. Neither uses the in-repo shim.

Data: 500001 distinct rows, four columns, more than 10 MB, parsed once for isolated scans. Name indexed scans do 12 repetitions (~6 million cells), actual cell-proxy iterator scans 4 repetitions. Dirty frames edit every 499th row; mapped dirty frame additionally hides the id column. Quoted indexed scan exercises realized arena storage. chunk_parallel_apply includes fresh stream parsing, chunk creation and executor(2) scanning all columns with chunk size 50000. One untimed warmup per executable followed by 7 paired samples, order alternated each pair. Agent CPU workloads were serialized.

All checksums agree across all 14 measured runs for each metric. Raw samples: [compiled-library runs](performance/direct-library-samples.json) and [generated-header runs](performance/direct-header-samples.json). Summaries contain medians, min/max, IQR and checksums. No sampling instrumentation or forced inlining.

## Median results (milliseconds; negative percentage means less time)

| Metric | Multiheader baseline | Multiheader candidate | Change | Header-only baseline | Header-only candidate | Change |
|---|---:|---:|---:|---:|---:|---:|
| clean_indexed | 81.48 | 49.40 | -39.4% | 82.34 | 44.05 | -46.5% |
| clean_iterator | 61.25 | 61.76 | +0.8% | 62.90 | 62.47 | -0.7% |
| clean_quoted_indexed | 90.60 | 78.09 | -13.8% | 128.26 | 74.34 | -42.0% |
| dirty_indexed | 86.10 | 53.66 | -37.7% | 87.80 | 49.53 | -43.6% |
| dirty_iterator | 60.28 | 62.68 | +4.0% | 63.00 | 62.37 | -1.0% |
| mapped_indexed | 86.80 | 53.58 | -38.3% | 87.03 | 52.65 | -39.5% |
| mapped_iterator | 61.84 | 61.84 | -0.0% | 62.69 | 62.87 | +0.3% |
| chunk_parallel | 175.45 | 130.28 | -25.7% | 159.33 | 120.23 | -24.5% |

The indexed improvements were consistently larger than their within-build dispersion; e.g. multiheader clean IQR 3.06 ms baseline/5.40 ms candidate versus 32.08 ms median reduction. Header clean IQR 9.86 ms/4.24 ms versus 38.30 ms reduction. chunk_parallel is noisier (multiheader IQR 24.67 ms/15.07 ms; header 17.70 ms/6.20 ms), but all 7 candidate times are below their paired baseline times in both build styles. A few shared-runtime outliers remain; full samples retain them. Iterator differences fall within dispersion and the implementation path is unchanged.

## Correctness and size

- Full DataFrame and ETL copied-header regressions: 109016 assertions across 69 cases pass.
- Supplemental view-equivalence test (subsequently integrated into `tests/test_data_frame_dirty.cpp`): 54 assertions on mmap and stream paths, 500001 rows, quote realization, configured trimming, eager scalar classification, empty fields, first/sparse edit visibility, hidden-column mapping, lossless materialization, row bounds, and invalidated proxies pass.
- Same supplemental regressions against generated candidate header with MSVC C++14: 54 assertions pass. The change uses only C++11 constructs; true C++11 compiler execution is not available locally.
- Multiheader executable 228864 → 229376 bytes (+512 B); header-only executable 207360 bytes unchanged. Executable file size is a coarse size check, not exact text-section size.

## Recommendation and limits

Retain direct extraction and leave out physical-column caching. It is a small private extraction reusing existing behavior and produced substantial indexed-scan improvement on this machine; both distribution styles benefit in these measurements. It keeps schema/dirty dispatch decisions in concrete backends/selector.

The experiment does not improve DataFrameColumn iterator traversal; that still constructs DataFrameCell proxies. These are microbenchmark and one representative chunked pipeline measurements on one compiler/processor, not universal expected percentages. No GCC/Clang performance, eager-scalar performance, or concurrent edit stress benchmark was run. Shared library benchmarks use identical old csv.lib for both variants; the added friendship changes no ABI and all changed functions are inline, and generated-header results independently avoid that mixing.

## Rejected alternative: physical-column caching

Recommendation: do not integrate this prototype. It shows a small clean-scan gain but no broad improvement, slows dirty indexed access, increases DFC size, and adds constructor work.

### Design

An eight-byte cached physical column index is resolved once at DFC construction. Cached views still resolve the current clean/dirty backend through DataFrameStorage every read; only schema validation and physical mapping are skipped. No clean-backend pointer or overlay snapshot is cached. Invalid manually constructed DFCs retain deferred validation through a SIZE_MAX sentinel fallback. Structural operations are exclusive and invalidate DFC generations; copying/moving a DFC copies/moves the cache with its existing generation token.

Changed four copied headers: DataFrameColumn, DataFrameStorage, CleanDataFrame, DirtyDataFrame. The experimental [patch](performance/cached-column.patch) is retained for reproduction against the baseline commit; it is not applied to the implementation. No public API or C++11 language feature changes; MSVC C++14 syntax check passed. Actual C++11 compiler unavailable locally.

### Measurements

MSVC /O2 /MD /std:c++20, the same retained `tools/benchmarks/data_frame/benchmark.cpp`, cached canonical csv.lib. Intel i5-12400, six cores/twelve logical. Warmup followed by seven paired baseline/candidate runs, alternating order. Other agents held all tests and compilation during timed runs. Shared dataset: 500001 distinct rows, clean, sparse-dirty, dirty with erased-column mapping; actual DFC iterator and chunk_parallel_apply with two workers. Indexed scans twelve repetitions, iterators four. All checksums matched.

Times below are milliseconds; change is candidate/baseline - 1, so negative is faster. MAD is median absolute deviation.

| Measurement | Baseline median | Candidate median | Change | Baseline MAD | Candidate MAD |
|---|---:|---:|---:|---:|---:|
| clean_indexed | 86.855 | 82.426 | -5.1% | 2.519 | 4.792 |
| clean_iterator | 63.233 | 60.031 | -5.1% | 2.738 | 1.556 |
| clean_quoted_indexed | 92.231 | 89.415 | -3.1% | 1.162 | 6.184 |
| dirty_indexed | 86.581 | 92.883 | +7.3% | 1.787 | 4.489 |
| dirty_iterator | 62.633 | 60.824 | -2.9% | 1.764 | 0.474 |
| mapped_indexed | 84.686 | 89.064 | +5.2% | 1.236 | 6.461 |
| mapped_iterator | 63.395 | 63.787 | +0.6% | 2.859 | 2.656 |
| chunk_parallel | 166.242 | 167.541 | +0.8% | 10.237 | 1.661 |

DFC layout: 48 bytes baseline, 56 bytes candidate. DataFrameCell layout unchanged. Each callback-by-value DFC copy carries an extra size_t; no allocation is added.

Full samples, checksums, median/min/max/MAD: [cached-column-results.json](performance/cached-column-results.json). There is meaningful noise: candidate clean_indexed ranges 71.903–134.306 ms despite its 82.426 ms median. Clean gains are modest and should not be overinterpreted. Dirty indexed median regressions are a reason to reject this as a general improvement.

A plausible explanation is that replacing schema/mapping checks with an extra sentinel branch changes code layout while much of dirty cost remains in selector/slot/overlay access. That is a hypothesis, not established causality. No assembly analysis or counter measurements were performed.

### Supplemental constructor/copy cost

Separate [construction.cpp](../../../tools/benchmarks/data_frame/construction.cpp); same compiler/link setup, warmup + seven alternating pairs. Six million emplacements/copies across twelve batches of 500000 DFCs, vector capacity pre-reserved; measurements include clear/destruction between batches and a final 500000-view get_sv checksum scan. This is a batch-cost estimate, not a precise isolated per-constructor cost.

| Batch | Baseline median | Candidate median | Change |
|---|---:|---:|---:|
| construct | 84.925 | 102.388 | +20.6% |
| copy | 81.674 | 85.393 | +4.6% |

Construction now reads schema validity and resolves physical mapping through the selector once, in addition to existing factory/name lookup validation. The measured construction batch is +20.6%; copying is +4.6%. Supplement data: [construction results](performance/cached-column-construction-results.json).

### Correctness

Copied-header full DataFrame + dirty + ETL test suite: 69 cases / 109016 assertions passed. Separate probe.cpp uses both mmap and stream with 500001 rows. Baseline/candidate output matches exactly for deferred invalid public constructors, null/default views, invalid row/column exception ordering, ragged bounds, copied/moved DFCs, captured clean views observing first edit, dirty physical mapping, and structural invalidation. Valid mapped values and subsequent edits also matched. C++14 /c probe compiled successfully. C++14 linkage against cached C++20 csv.lib is intentionally not used because their string_view ABI differs.

## Reproduction

The retained [benchmark source](../../../tools/benchmarks/data_frame/benchmark.cpp) and [paired runner](../../../tools/benchmarks/data_frame/run.py) reproduce the timed workload. Use two isolated checkouts: the baseline commit above and the implementation commit containing direct extraction. For the rejected caching experiment, apply `performance/cached-column.patch` to the baseline instead. Keep the retained harness identical across checkouts.

In an x64 MSVC developer command prompt, configure/build each checkout and compile the harness against its library:

```text
cmake -S . -B build/dfc-perf -DCSV_CXX_STANDARD=20 -DCSV_BUILD_TESTS=OFF
cmake --build build/dfc-perf --config Release --target csv
cl /nologo /EHsc /O2 /MD /std:c++20 /I include <retained-benchmark.cpp> build/dfc-perf/include/internal/Release/csv.lib /Fe:<output.exe> /Fo:<output.obj>
```

For generated-header mode, first build `generate_single_header` in each checkout, then compile with `/I build/dfc-perf/single_include_generated` and omit the library argument. This requires the canonical `single_header` generator available to CMake. Never compile against the in-repository `single_include/csv.hpp` shim.

Run the executable pair serially on an otherwise idle machine:

```text
python tools/benchmarks/data_frame/run.py <baseline.exe> <candidate.exe> --output build/dfc-perf-results
```

The runner performs warmups, alternates pair order, verifies checksums, and retains raw samples and median/range/IQR summaries. Use the same build mode/compiler flags for each pair; compare variants within a pair rather than absolute times across build styles. Standalone benchmark sources live under `tools/benchmarks/data_frame/`, outside the amalgamation input tree. They are manual investigation tools and are not part of the library or test targets.
