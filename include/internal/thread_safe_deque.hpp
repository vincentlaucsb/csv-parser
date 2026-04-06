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
#include "common.hpp"
#include <deque>

#if CSV_ENABLE_THREADS
#include <atomic>
#include <condition_variable>
#include <mutex>
#endif

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
#if CSV_ENABLE_THREADS
                this->_is_empty.store(other._is_empty.load(std::memory_order_acquire), std::memory_order_release);
#else
                this->_is_empty = other._is_empty;
                this->_is_waitable = other._is_waitable;
#endif
            }

            ThreadSafeDeque(const std::deque<T>& source) : ThreadSafeDeque() {
                this->data = source;
#if CSV_ENABLE_THREADS
                this->_is_empty.store(source.empty(), std::memory_order_release);
#else
                this->_is_empty = source.empty();
#endif
            }

            bool empty() const noexcept {
#if CSV_ENABLE_THREADS
                return this->_is_empty.load(std::memory_order_acquire);
#else
                return this->_is_empty;
#endif
            }

            T& front() noexcept {
#if CSV_ENABLE_THREADS
                std::lock_guard<std::mutex> lock{ this->_lock };
#endif
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
#if CSV_ENABLE_THREADS
                std::lock_guard<std::mutex> lock{ this->_lock };
#endif
                this->data.push_back(std::move(item));
#if CSV_ENABLE_THREADS
                this->_is_empty.store(false, std::memory_order_release);
#else
                this->_is_empty = false;
#endif

#if CSV_ENABLE_THREADS
                if (this->data.size() >= _notify_size) {
                    this->_cond.notify_all();
                }
#endif
            }

            T pop_front() noexcept {
#if CSV_ENABLE_THREADS
                std::lock_guard<std::mutex> lock{ this->_lock };
#endif
                T item = std::move(data.front());
                data.pop_front();
                
                // Update empty flag if we just emptied the deque
                if (this->data.empty()) {
#if CSV_ENABLE_THREADS
                    this->_is_empty.store(true, std::memory_order_release);
#else
                    this->_is_empty = true;
#endif
                }
                
                return item;
            }

            /** Returns true if a thread is actively pushing items to this deque */
            bool is_waitable() const noexcept {
#if CSV_ENABLE_THREADS
                return this->_is_waitable.load(std::memory_order_acquire);
#else
                return this->_is_waitable;
#endif
            }

            /** Wait for an item to become available */
            void wait() {
#if CSV_ENABLE_THREADS
                if (!is_waitable()) {
                    return;
                }

                std::unique_lock<std::mutex> lock{ this->_lock };
                this->_cond.wait(lock, [this] { return this->data.size() >= _notify_size || !this->is_waitable(); });
                lock.unlock();
#endif
            }

            size_t size() const noexcept {
#if CSV_ENABLE_THREADS
                std::lock_guard<std::mutex> lock{ this->_lock };
#endif
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
#if CSV_ENABLE_THREADS
                std::lock_guard<std::mutex> lock{ this->_lock };
                this->_is_waitable.store(true, std::memory_order_release);
                this->_cond.notify_all();
#else
                this->_is_waitable = true;
#endif
            }

            /** Tell all listeners to stop */
            void kill_all() {
#if CSV_ENABLE_THREADS
                std::lock_guard<std::mutex> lock{ this->_lock };
                this->_is_waitable.store(false, std::memory_order_release);
                this->_cond.notify_all();
#else
                this->_is_waitable = false;
#endif
            }

        private:
#if CSV_ENABLE_THREADS
            std::atomic<bool> _is_empty{ true };     // Lock-free empty() check  
            std::atomic<bool> _is_waitable{ false }; // Lock-free is_waitable() check
#else
            bool _is_empty = true;
            bool _is_waitable = false;
#endif
            size_t _notify_size;
#if CSV_ENABLE_THREADS
            mutable std::mutex _lock;
            std::condition_variable _cond;
#endif
            std::deque<T> data;
        };
    }
}
