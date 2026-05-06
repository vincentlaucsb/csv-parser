/** @file
 *  @brief Thread-safe deque for producer-consumer patterns
 * 
 *  Generic container used for cross-thread communication in the CSV parser.
 *  Parser thread pushes rows, main thread pops them.
 *
 *  Design notes: see THREADSAFE_DEQUE_DESIGN.md for protocol details,
 *  invariants, and producer/consumer timing diagrams.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

#include "row_queue_batch.hpp"
#include "row_queue_inspection.hpp"

namespace csv {
    namespace internals {
        /** A std::deque wrapper which allows multiple read and write threads to concurrently
         *  access it along with providing read threads the ability to wait for the deque
         *  to become populated.
         *
         *  Concurrency strategy: writer-side mutations (push_back/pop_front) are locked;
         *  hot-path flags (empty/is_waitable) are atomic; inspect() is the synchronized
         *  observation path for tests and diagnostics.
         */
        template<typename T>
        class ThreadSafeDeque {
        public:
            ThreadSafeDeque(size_t notify_size = 100) : _notify_size(notify_size) {}

            ThreadSafeDeque(const ThreadSafeDeque& other) {
                std::lock_guard<std::mutex> lock{ other._lock };
                this->batches_ = other.batches_;
                this->front_index_ = other.front_index_;
                this->size_ = other.size_;
                this->_notify_size = other._notify_size;
                this->_is_empty.store(other._is_empty.load(std::memory_order_acquire), std::memory_order_release);
                this->_is_waitable.store(other._is_waitable.load(std::memory_order_acquire), std::memory_order_release);
            }

            ThreadSafeDeque(const std::deque<T>& source) : ThreadSafeDeque() {
                std::vector<T> rows;
                rows.reserve(source.size());
                for (const auto& row : source) {
                    rows.push_back(row);
                }
                if (!rows.empty()) {
                    this->batches_.push_back(std::move(rows));
                    this->size_ = source.size();
                }
                this->_is_empty.store(source.empty(), std::memory_order_release);
            }

            bool empty() const noexcept {
                return this->_is_empty.load(std::memory_order_acquire);
            }

            void push_back(T&& item) {
                std::lock_guard<std::mutex> lock{ this->_lock };
                std::vector<T> batch;
                batch.push_back(std::move(item));
                this->batches_.push_back(std::move(batch));
                this->size_++;
                this->_is_empty.store(false, std::memory_order_release);

                if (this->size_ >= _notify_size) {
                    this->_cond.notify_all();
                }
            }

            void append_rows(std::vector<T>&& rows) {
                if (rows.empty()) {
                    return;
                }

                std::lock_guard<std::mutex> lock{ this->_lock };
                this->size_ += rows.size();
                this->batches_.push_back(std::move(rows));
                this->_is_empty.store(false, std::memory_order_release);

                if (this->size_ >= _notify_size) {
                    this->_cond.notify_all();
                }
            }

            T pop_front() noexcept {
                std::lock_guard<std::mutex> lock{ this->_lock };
                T item = std::move(this->batches_.front()[this->front_index_]);
                this->front_index_++;
                this->size_--;
                this->discard_exhausted_front_batch();

                if (this->size_ == 0) {
                    this->_is_empty.store(true, std::memory_order_release);
                }

                return item;
            }

            /** Move up to @p max_items rows into a caller-owned batch buffer under one lock.
             *
             *  This is the preferred consumer path for chunked reads: it preserves queue
             *  semantics while amortizing mutex traffic across many rows. Complete queued
             *  batches are moved as contiguous spans; per-row moves only remain when the
             *  requested limit splits a batch.
             */
            size_t drain_front(std::vector<T>& out, size_t max_items) {
                std::lock_guard<std::mutex> lock{ this->_lock };
                const size_t drain_count = drain_front_batches(
                    this->batches_,
                    this->front_index_,
                    this->size_,
                    out,
                    max_items
                );

                if (this->size_ == 0) {
                    this->_is_empty.store(true, std::memory_order_release);
                }

                return drain_count;
            }

            /** Invoke @p callback with a stable const view of queued rows under one lock.
             *
             *  This is intended for diagnostics and tests that need to inspect
             *  queue contents without using unsynchronized random access.
             */
            template<typename Callback>
            void inspect(Callback&& callback) const {
                std::lock_guard<std::mutex> lock{ this->_lock };
                RowQueueInspectionView<T> view(this->batches_, this->front_index_, this->size_);
                std::forward<Callback>(callback)(view);
            }

            /** Returns true if a thread is actively pushing items to this deque */
            bool is_waitable() const noexcept {
                return this->_is_waitable.load(std::memory_order_acquire);
            }

            void wait() {
                if (!is_waitable()) {
                    return;
                }

                std::unique_lock<std::mutex> lock{ this->_lock };
                this->_cond.wait(lock, [this] { return this->size_ >= _notify_size || !this->is_waitable(); });
                lock.unlock();
            }

            size_t size() const noexcept {
                std::lock_guard<std::mutex> lock{ this->_lock };
                return this->size_;
            }

            /** Tell listeners that this deque is actively being pushed to */
            void notify_all() {
                std::lock_guard<std::mutex> lock{ this->_lock };
                this->_is_waitable.store(true, std::memory_order_release);
                this->_cond.notify_all();
            }

            void kill_all() {
                std::lock_guard<std::mutex> lock{ this->_lock };
                this->_is_waitable.store(false, std::memory_order_release);
                this->_cond.notify_all();
            }

        private:
            std::atomic<bool> _is_empty{ true };      // Lock-free empty() check
            std::atomic<bool> _is_waitable{ false };  // Lock-free is_waitable() check
            size_t _notify_size;
            mutable std::mutex _lock;
            std::condition_variable _cond;
            std::deque<std::vector<T>> batches_;
            size_t front_index_ = 0;
            size_t size_ = 0;

            void discard_exhausted_front_batch() noexcept {
                while (!this->batches_.empty() && this->front_index_ >= this->batches_.front().size()) {
                    this->batches_.pop_front();
                    this->front_index_ = 0;
                }
            }
        };
    }
}
