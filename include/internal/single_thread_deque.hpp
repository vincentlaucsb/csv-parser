/** @file
 *  @brief Single-threaded row deque implementation
 */

#pragma once

#include <deque>
#include <utility>
#include <vector>

namespace csv {
    namespace internals {
        template<typename T>
        class SingleThreadDeque {
        public:
            SingleThreadDeque(size_t notify_size = 100) : _notify_size(notify_size) {}

            SingleThreadDeque(const SingleThreadDeque& other) {
                this->data = other.data;
                this->_notify_size = other._notify_size;
                this->_is_empty = other._is_empty;
                this->_is_waitable = other._is_waitable;
            }

            SingleThreadDeque(const std::deque<T>& source) : SingleThreadDeque() {
                this->data = source;
                this->_is_empty = source.empty();
            }

            bool empty() const noexcept {
                return this->_is_empty;
            }

            T& front() noexcept {
                return this->data.front();
            }

            T& operator[](size_t n) {
                return this->data[n];
            }

            void push_back(T&& item) {
                this->data.push_back(std::move(item));
                this->_is_empty = false;
            }

            T pop_front() noexcept {
                T item = std::move(data.front());
                data.pop_front();

                if (this->data.empty()) {
                    this->_is_empty = true;
                }

                return item;
            }

            /** Move up to @p max_items rows into a caller-owned batch buffer. */
            size_t drain_front(std::vector<T>& out, size_t max_items) {
                const size_t available = this->data.size();
                const size_t drain_count = available < max_items ? available : max_items;

                for (size_t i = 0; i < drain_count; ++i) {
                    out.push_back(std::move(this->data.front()));
                    this->data.pop_front();
                }

                if (this->data.empty()) {
                    this->_is_empty = true;
                }

                return drain_count;
            }

            bool is_waitable() const noexcept {
                return this->_is_waitable;
            }

            void wait() {
                // No-op in single-thread mode.
            }

            size_t size() const noexcept {
                return this->data.size();
            }

            typename std::deque<T>::iterator begin() noexcept {
                return this->data.begin();
            }

            typename std::deque<T>::iterator end() noexcept {
                return this->data.end();
            }

            void notify_all() {
                this->_is_waitable = true;
            }

            void kill_all() {
                this->_is_waitable = false;
            }

        private:
            bool _is_empty = true;
            bool _is_waitable = false;
            size_t _notify_size;
            std::deque<T> data;
        };
    }
}
