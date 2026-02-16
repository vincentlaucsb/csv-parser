/** @file
 *  @brief Thread-safe deque for producer-consumer patterns
 * 
 *  Generic container used for cross-thread communication in the CSV parser.
 *  Parser thread pushes rows, main thread pops them.
 */

#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace csv {
    namespace internals {
        /** A std::deque wrapper which allows multiple read and write threads to concurrently
         *  access it along with providing read threads the ability to wait for the deque
         *  to become populated
         */
        template<typename T>
        class ThreadSafeDeque {
        public:
            ThreadSafeDeque(size_t notify_size = 100) : _notify_size(notify_size) {};
            ThreadSafeDeque(const ThreadSafeDeque& other) {
                this->data = other.data;
                this->_notify_size = other._notify_size;
                this->_is_empty.store(other._is_empty.load(std::memory_order_acquire), std::memory_order_release);
            }

            ThreadSafeDeque(const std::deque<T>& source) : ThreadSafeDeque() {
                this->data = source;
                this->_is_empty.store(source.empty(), std::memory_order_release);
            }

            void clear() noexcept { 
                this->data.clear(); 
                this->_is_empty.store(true, std::memory_order_release);
            }

            bool empty() const noexcept {
                return this->_is_empty.load(std::memory_order_acquire);
            }

            T& front() noexcept {
                std::lock_guard<std::mutex> lock{ this->_lock };
                return this->data.front();
            }

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
                
                // Update empty flag if we just emptied the deque
                if (this->data.empty()) {
                    this->_is_empty.store(true, std::memory_order_release);
                }
                
                return item;
            }

            /** Returns true if a thread is actively pushing items to this deque */
            constexpr bool is_waitable() const noexcept { return this->_is_waitable; }

            /** Wait for an item to become available */
            void wait() {
                if (!is_waitable()) {
                    return;
                }

                std::unique_lock<std::mutex> lock{ this->_lock };
                this->_cond.wait(lock, [this] { return this->data.size() >= _notify_size || !this->is_waitable(); });
                lock.unlock();
            }

            typename std::deque<T>::iterator begin() noexcept {
                return this->data.begin();
            }

            typename std::deque<T>::iterator end() noexcept {
                return this->data.end();
            }

            /** Tell listeners that this deque is actively being pushed to */
            void notify_all() {
                std::unique_lock<std::mutex> lock{ this->_lock };
                this->_is_waitable = true;
                this->_cond.notify_all();
            }

            /** Tell all listeners to stop */
            void kill_all() {
                std::unique_lock<std::mutex> lock{ this->_lock };
                this->_is_waitable = false;
                this->_cond.notify_all();
            }

        private:
            std::atomic<bool> _is_empty{ true };     // Lock-free empty() check  
            std::atomic<bool> _is_waitable{ false }; // Lock-free is_waitable() check
            size_t _notify_size;
            mutable std::mutex _lock;
            std::condition_variable _cond;
            std::deque<T> data;
        };
    }
}
