# ThreadSafeDeque Synchronization Protocol

## Overview

`ThreadSafeDeque` implements a producer-consumer queue used by the CSV parser to communicate between the worker thread (producer) and the main thread (consumer). This document explains the synchronization protocol, critical invariants, and the exact sequence of operations required for correct concurrency.

## Signal Semantics (High-Level)

Before diving into lock ordering, define the two key queue signals in parser terms:

- `is_waitable() == true`: A CSV parsing worker is currently active for this read cycle. More rows may still be pushed, so the consumer is allowed to wait.
- `is_waitable() == false`: The current worker has finished pushing rows for this cycle and has published a terminal notification (`kill_all()`).

Important nuance:

- `is_waitable() == false` does **not** always mean global end-of-file by itself.
- It means: no more rows will be pushed by the **current** active worker.
- `CSVReader::read_row()` may start another worker for the next chunk when parser EOF has not been reached yet.
- If parser EOF is true, then `is_waitable() == false` also implies no more CSV rows remain to be parsed.

## Architectural Note: Consumer-Driven Pacing

The producer is intentionally not always running.

- It parses one chunk, enqueues rows, and stops for that cycle.
- The consumer drains queued rows and only then triggers the next worker cycle if EOF is not reached.
- This keeps parsed rows bounded in memory instead of allowing unbounded producer run-ahead.

## Two-Thread Model

| Step | Consumer (read_row) | Producer (read_csv worker) | Shared State / Note |
|------|----------------------|----------------------------|---------------------|
| 1 | `empty()` check | | Fast-path queue visibility via `_is_empty` |
| 2 | `is_waitable()` check | | If true, consumer may block; if false, consumer should not wait |
| 3 | `wait()` | | Blocks until `data.size() >= notify_size` or `!is_waitable()` |
| 4 | | `push_back(row)` | Producer appends rows and sets `_is_empty = false` |
| 5 | | size-based `notify_all()` (when `size >= notify_size`) | Batch wake-up for throughput |
| 6 | | `kill_all()` at end of worker cycle | Sets `_is_waitable = false` and terminal-notifies waiters |
| 7 | `pop_front()` | | Consumer drains rows after wake-up |

## Synchronization Points

### 1. **Early Guard Check in `wait()`**

**Location:** [thread_safe_deque.hpp](thread_safe_deque.hpp)

```cpp
void wait() {
    if (!is_waitable()) {  // <-- ATOMIC READ, NO LOCK
        return;            // Fast path: producer already done
    }

    std::unique_lock<std::mutex> lock{ this->_lock };
    this->_cond.wait(lock, [this] {
        return this->data.size() >= _notify_size || !this->is_waitable();
    });
    lock.unlock();
}
```

**Why atomic:** Cheap lockfree check to avoid lock contention when parser has already finished.

**Invariant:** If `is_waitable() == false`, the producer will never push() again and a prior `notify_all()` was already called.

---

### 2. **Notification Without Lock = DEADLOCK (The Bug)**

**BEFORE PR 298 (BUGGY):**

```cpp
// In kill_all() - NO LOCK
void kill_all() {
    this->_is_waitable.store(false, std::memory_order_release);  // T1: set flag
    this->_cond.notify_all();                                     // T2: signal
    // Race window here!
}
```

**Race Timeline (Small CSV, < 100 rows):**

| Time | Consumer Thread | Producer Thread |
|------|-----------------|-----------------|
| T0   | records->empty() == true | |
| T1   | records->is_waitable() == true (atomic read) | |
| T2   | | kill_all() executes: |
| T3   | | _is_waitable.store(false) |
| T4   | | _cond.notify_all() -- **SIGNAL SENT** |
| T5   | **ENTERS wait()** acquiring lock | |
| T6   | _cond.wait() blocks forever | |
| **DEADLOCK** | Waiting for notification that was already sent | Thread exits |

**Root Cause:** The notification in step T4 is sent *before* the consumer enters wait() in step T5. The condition variable predicate was already true (`!is_waitable()`), but the consumer missed the signal.

---

### 3. **Notification WITH Lock = CORRECT**

**AFTER PR 298 (FIXED):**

```cpp
// In kill_all() - ACQUIRES LOCK
void kill_all() {
    std::lock_guard<std::mutex> lock{ this->_lock };  // <-- LOCK HELD
    this->_is_waitable.store(false, std::memory_order_release);
    this->_cond.notify_all();
}  // <-- LOCK RELEASED
```

**Corrected Timeline:**

| Time | Consumer Thread | Producer Thread |
|------|-----------------|-----------------|
| T0   | records->empty() == true | |
| T1   | records->is_waitable() == true (atomic read) | |
| T2   | | kill_all() attempts to acquire _lock |
| T3   | | **_lock ACQUIRED** |
| T4   | | _is_waitable.store(false) |
| T5   | std::unique_lock<> lock{_lock} -- **BLOCKS** | |
| T6   | | _cond.notify_all() |
| T7   | | **_lock RELEASED** |
| T8   | **LOCK ACQUIRED** | |
| T9   | Predicate: is_waitable() == false → true! | |
| T10  | wake() returns immediately | |
| **CORRECT** | Consumer wakes and exits wait | |

**Key Insight:** The mutex serializes the state transition and notification with the consumer's lock acquisition. The consumer **cannot** enter wait() until the producer has either:
- Already released the lock (and notification was sent inside the critical section), or
- Never acquired it (fast path via atomic early check)

---

## Atomic vs. Mutex Layering

### Why Both Are Needed

**`_is_empty` (atomic):**
- `empty()` is called millions of times per parse
- Hot-path: no lock overhead
- Acquire-release semantics ensure visibility
- Safe because we only need a flag, not structured data

**`_is_waitable` (atomic):**
- Early guard in `wait()` avoids lock when producer is done
- Cheap check before expensive lock acquisition
- But transitions must be protected by mutex

**`_lock` + `_cond` (mutex + condition variable):**
- Protects the **critical** operation: waiting for data or EOF
- Ensures state change + notification are atomic from waiter's perspective
- Producer calls `notify_all()` while holding lock
- Consumer checks predicate while holding lock

### Why Atomic-Only Is Wrong

```cpp
// WRONG: Do not do this
void kill_all() {
    this->_is_waitable.store(false, std::memory_order_release);
    this->_cond.notify_all();  // <-- RACE!
}
```

The atomic store has memory ordering but does **not** prevent the lost-wakeup window. Atomics guarantee visibility, not synchronization with condition variables.

---

## Batching Predicate: The `notify_size` Parameter

**Location:** [thread_safe_deque.hpp](thread_safe_deque.hpp)

```cpp
ThreadSafeDeque(size_t notify_size = 100) : _notify_size(notify_size) {}

// In push_back():
if (this->data.size() >= _notify_size) {
    this->_cond.notify_all();
}

// In wait():
this->_cond.wait(lock, [this] {
    return this->data.size() >= _notify_size || !this->is_waitable();
});
```

**Why batching matters:**
- CSV parser produces rows in 10MB chunks (~thousands of rows)
- Without batching, every `push_back()` would wake the consumer
- Context switching overhead would dominate
- Default: wake every 100 rows (compromise between latency and throughput)

**Small file edge case:**
- CSV with 2 rows (< 100)
- push_back() never triggers the size threshold
- Parser completes and calls kill_all()
- **Only** the terminal `kill_all()` notification allows consumer to wake
- This is why the lock in kill_all() is critical

---

## Operation Timeline: Full Example

**CSV: 2 rows, stream source**

```
t=0:   Consumer: read_row() → records->empty() == true
       Consumer: is_waitable() == true → go to wait()

t=1:   Producer: read_csv() worker thread starts
       Producer: notify_all() → _is_waitable = true, signal

t=2:   Producer: parser->next() reads and parses 2 rows
       Producer: push_back(row1) → data.size() = 1 (< 100, no signal)
       Producer: push_back(row2) → data.size() = 2 (< 100, no signal)

t=3:   Producer: kill_all() -- ACQUIRES LOCK
       Producer: _is_waitable = false, notify_all()
       Producer: RELEASES LOCK

t=4:   Consumer: ACQUIRES LOCK (was blocked in wait())
       Consumer: Predicate: size >= 100? NO
                           !is_waitable()? YES! ← Wakes
       Consumer: RELEASES LOCK, returns from wait()

t=5:   Consumer: pop_front() → returns row1
```

---

## Critical Invariants

1. **No Lost Wakeups:** If producer publishes terminal state (`_is_waitable = false`), consumer **will** wake.
   - Requires: lock held during state change and notify

2. **Atomic Reads Are Safe:** Early check `if (!is_waitable())` is safe without lock.
   - Requires: acquire-release semantics on atomic store/load

3. **Empty Flag Accuracy:** `empty()` returns true iff deque is empty.
   - Requires: lock held during push/pop and flag update

4. **No Double-Wake:** Notification happens exactly once per state transition.
   - Requires: batching predicate or terminal condition

---

## Code Locations (CSV Parser Flow)

| Component | Where | What |
|-----------|-------|------|
| **Consumer** | [csv_reader.cpp](../csv_reader.cpp) | `read_row()` flow on main thread |
| **Consumer** | [csv_reader.cpp](../csv_reader.cpp) | Empty/is_waitable checks and wait() call |
| **Producer** | [csv_reader.cpp](../csv_reader.cpp) | `read_csv()` worker lifecycle |
| **Producer** | [csv_reader.cpp](../csv_reader.cpp) | notify_all() at cycle start |
| **Producer** | [csv_reader.cpp](../csv_reader.cpp) | parser->next() push cycle |
| **Producer** | [csv_reader.cpp](../csv_reader.cpp) | kill_all() terminal signal |
| **Queue** | [thread_safe_deque.hpp](thread_safe_deque.hpp) | push_back() producer path |
| **Queue** | [thread_safe_deque.hpp](thread_safe_deque.hpp) | pop_front() consumer path |
| **Queue** | [thread_safe_deque.hpp](thread_safe_deque.hpp) | wait() condition protocol |
| **Queue** | [thread_safe_deque.hpp](thread_safe_deque.hpp) | notify_all() wake-up state |
| **Queue** | [thread_safe_deque.hpp](thread_safe_deque.hpp) | kill_all() terminal publication |

---

## Testing Coverage

See [test_threadsafe_deque_race.cpp](../../tests/test_threadsafe_deque_race.cpp) for regression tests that specifically target:
- Small CSV files (< 100 rows) that hit the terminal notification case
- Repeated iterations to increase race window probability
- Timeouts to prevent CI hangs on deadlock regression

The test helper [timeout_helper.hpp](../../tests/shared/timeout_helper.hpp) wraps stress tests with explicit 10-second timeouts.
