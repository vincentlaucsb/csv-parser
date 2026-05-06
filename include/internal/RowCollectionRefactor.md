# RowCollection Refactor TODO

## Problem

`RowCollection` currently aliases `ThreadSafeDeque<CSVRow>` (or
`SingleThreadDeque<CSVRow>` in no-thread builds). That queue was designed for
the original parser shape: one parser completes one `CSVRow` and pushes that
row immediately.

Speculative parsing adds a second producer shape:

- serial parsing still emits rows one at a time
- speculative workers parse rows into `std::vector<CSVRow>` batches
- the validator releases those batches in order after checking/fixing chunk
  boundary state

The validator is serial by design, so per-row queue overhead there is amplified.
Moving complete worker batches into the queue should be much cheaper than
re-pushing each row individually.

## Desired Shape

Replace the row queue internals with a batch-aware collection while preserving a
small consumer-facing API.

Producer APIs:

```cpp
void push_back(CSVRow&& row);                 // serial parser path
void append_rows(std::vector<CSVRow>&& rows); // speculative validator path
```

Consumer APIs should remain familiar:

```cpp
bool empty() const noexcept;
CSVRow pop_front();
size_t drain_front(std::vector<CSVRow>& out, size_t max_items);
void wait();
void notify_all();
void kill_all();
size_t size() const noexcept;
```

Possible internal storage:

```cpp
std::deque<std::vector<CSVRow>> batches_;
std::vector<CSVRow> pending_single_rows_;
size_t front_index_ = 0;
size_t total_rows_ = 0;
```

When fed single rows, the queue accumulates them into `pending_single_rows_` and
flushes that vector into `batches_` once it reaches the notification/batch size
or when the producer finishes. When fed a vector, the queue flushes pending
single rows, takes ownership of the vector, and updates bookkeeping.

## Staged Plan

1. Add `append_rows(std::vector<CSVRow>&&)` to the existing deque-backed
   `ThreadSafeDeque` and `SingleThreadDeque`.
   - Move rows under one lock.
   - Update `RowDequeLike`.
   - Teach speculative validation to release `complete_rows` in one call.

2. Replace the internal `std::deque<CSVRow>` storage with batch-aware storage.
   - Preserve `push_back`, `pop_front`, and `drain_front` behavior.
   - Track `total_rows_` explicitly so `empty()` and `size()` stay cheap.
   - Keep notification semantics based on total queued rows.

3. Revisit awkward legacy APIs.
   - `front()`, `operator[]`, and iterators are less natural over batch storage.
   - Confirm whether production code needs them.
   - Prefer removing or limiting them if tests only used them as convenience.

## Correctness Traps

- Preserve the no-lost-wakeup protocol documented in `THREADSAFE_DEQUE_DESIGN.md`.
- `kill_all()` must flush pending single-row batches before publishing terminal
  state, or the consumer can observe completion before seeing all rows.
- `append_rows({})` should be a no-op.
- `drain_front()` must preserve row order across batch boundaries.
- `read_row()` and `read_chunk()` must continue to share the same queue.
- No-thread builds must expose the same API as threaded builds.

## Out Of Scope

- Tuple/typed sinks.
- Parser validation policy.
- Speculative scanner heuristics.
- Public `CSVReader` API changes.

