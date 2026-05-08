@page thread_pool_refactor Thread Pool Refactor Notes

# Thread Pool Refactor Notes

`DataFrameExecutor` and `speculative::ParallelCSVParser` both contain a
persistent indexed-task worker pool. They should eventually share a small
internal executor instead of maintaining two near-identical scheduling loops.

## Shared Concept

Both implementations:

- own a fixed vector of worker threads
- wake workers with a generation counter
- assign work by monotonically increasing task index
- stop dispatching after the first worker exception
- capture `std::exception_ptr` in the worker and rethrow on the caller thread
- wait for all workers in the current generation before returning
- keep worker threads alive across multiple calls

The common abstraction is an indexed task pool:

```cpp
pool.parallel_for(task_count, [](size_t worker_index, size_t task_index) {
    // do work
});
```

`worker_index` matters because the speculative parser needs worker-local parser
state (`ChunkParserCore`). `DataFrameExecutor` can ignore it.

## Differences To Preserve

- `DataFrameExecutor` exposes a public-ish batch API for DataFrame operations.
- `ParallelCSVParser` is an internal parser implementation detail.
- CSV parsing needs worker-local state and deterministic write-back to a parsed
  chunk array.
- DataFrame work currently only needs task indices.
- Single-thread builds must continue to compile the same public APIs without
  creating worker threads.

## Proposed Shape

Create an internal header, likely:

```text
include/internal/parallel/indexed_task_pool.hpp
```

Potential class:

```cpp
class IndexedTaskPool {
public:
    explicit IndexedTaskPool(size_t worker_count);
    ~IndexedTaskPool();

    size_t worker_count() const noexcept;

    template<typename Fn>
    void parallel_for(size_t task_count, Fn&& fn);
};
```

Behavior:

- If `worker_count <= 1`, run serially.
- If `task_count == 0`, return immediately.
- If a worker throws, stop assigning new tasks and rethrow on caller thread.
- Pass `(worker_index, task_index)` to the callback.
- Keep synchronization details private.

## Migration Plan

1. Add `IndexedTaskPool` and tests for:
   - serial fallback
   - indexed work distribution
   - exception propagation
   - no work for zero tasks
   - repeated generations
2. Change `DataFrameExecutor` to delegate to `IndexedTaskPool`.
3. Change `ParallelCSVParser` to delegate to `IndexedTaskPool`, using
   `worker_index` to address a worker-local `ChunkParserCore`.
4. Delete the duplicated worker loops.

## Not This Moment

This is a cleanup/risk-reduction refactor, but it is not the next performance
hot path. The tuple/parser-core policy refactor should land first because it
directly enables allocation-free typed parsing. TODO: re-expose/document a
public tuple-reader API only after it is backed by the parser-core typed path.
