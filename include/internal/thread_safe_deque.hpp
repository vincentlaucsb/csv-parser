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
#include <vector>

namespace csv {
    namespace internals {
        /** A std::deque wrapper which allows multiple read and write threads to concurrently
         *  access it along with providing read threads the ability to wait for the deque
         *  to become populated.
         *
         *  Concurrency strategy: writer-side mutations (push_back/pop_front) are locked;
         *  hot-path flags (empty/is_waitable) are atomic; operator[] and iterators are
         *  not synchronized and must not run concurrently with writers.
         */
        template<typename T>
        class ThreadSafeDeque {
        public:
            ThreadSafeDeque(size_t notify_size = 100) : _notify_size(notify_size) {}

            ThreadSafeDeque(const ThreadSafeDeque& other) {
                this->data = other.data;
                this->_notify_size = other._notify_size;
                this->_is_empty.store(other._is_empty.load(std::memory_order_acquire), std::memory_order_release);
            }

            ThreadSafeDeque(const std::deque<T>& source) : ThreadSafeDeque() {
                this->data = source;
                this->_is_empty.store(source.empty(), std::memory_order_release);
            }

            bool empty() const noexcept {
                return this->_is_empty.load(std::memory_order_acquire);
            }

            T& front() noexcept {
                std::lock_guard<std::mutex> lock{ this->_lock };
                return this->data.front();
            }

            /** NOTE: operator[] is not synchronized.
             *  Only call when no concurrent push_back/pop_front can occur.
             *  std::deque can reallocate its internal map on push_back, which
             *  makes concurrent operator[] access undefined behavior.
             */
            T& operator[](size_t n) {
                return this->data[n];
            }

            void push_back(T&& item) {
                std::lock_guard<std::mutex> lock{ this->_lock };
                this->data.push_back(std::move(item));
                this->_is_empty.store(false, std::memory_order_release);

                if (this->data.size() >= _notify_size) {
                    this->_cond.notify_all();
                }
            }

            T pop_front() noexcept {
                std::lock_guard<std::mutex> lock{ this->_lock };
                T item = std::move(data.front());
                data.pop_front();

                if (this->data.empty()) {
                    this->_is_empty.store(true, std::memory_order_release);
                }

                return item;
            }

            /** Move up to @p max_items rows into a caller-owned batch buffer under one lock.
             *
             *  This is the preferred consumer path for chunked reads: it preserves queue
             *  semantics while amortizing mutex traffic across many rows.
             */
            size_t drain_front(std::vector<T>& out, size_t max_items) {
                std::lock_guard<std::mutex> lock{ this->_lock };
                const size_t available = this->data.size();
                const size_t drain_count = available < max_items ? available : max_items;

                for (size_t i = 0; i < drain_count; ++i) {
                    out.push_back(std::move(this->data.front()));
                    this->data.pop_front();
                }

                if (this->data.empty()) {
                    this->_is_empty.store(true, std::memory_order_release);
                }

                return drain_count;
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
                this->_cond.wait(lock, [this] { return this->data.size() >= _notify_size || !this->is_waitable(); });
                lock.unlock();
            }

            size_t size() const noexcept {
                std::lock_guard<std::mutex> lock{ this->_lock };
                return this->data.size();
            }

            typename std::deque<T>::iterator begin() noexcept {
                return this->data.begin();
            }

            typename std::deque<T>::iterator end() noexcept {
                return this->data.end();
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
            std::deque<T> data;
        };
    }
}
