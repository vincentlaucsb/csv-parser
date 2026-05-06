/** @file
 *  @brief Single-threaded row deque implementation
 */

#pragma once

#include <cstddef>
#include <deque>
#include <iterator>
#include <utility>
#include <vector>

namespace csv {
    namespace internals {
        template<typename T>
        class SingleThreadDeque {
        public:
            SingleThreadDeque(size_t notify_size = 100) {
                (void)notify_size;
            }

            SingleThreadDeque(const SingleThreadDeque& other) {
                this->records_ = other.records_;
                this->_is_empty = other._is_empty;
                this->_is_waitable = other._is_waitable;
            }

            SingleThreadDeque(const std::deque<T>& source)
                : _is_empty(source.empty()),
                  records_(source) {}

            bool empty() const noexcept {
                return this->_is_empty;
            }

            void push_back(T&& item) {
                this->records_.push_back(std::move(item));
                this->_is_empty = false;
            }

            void append_rows(std::vector<T>&& rows) {
                if (rows.empty()) {
                    return;
                }

                this->records_.insert(
                    this->records_.end(),
                    std::make_move_iterator(rows.begin()),
                    std::make_move_iterator(rows.end())
                );
                this->_is_empty = false;
            }

            T pop_front() noexcept {
                T item = std::move(this->records_.front());
                this->records_.pop_front();

                if (this->records_.empty()) {
                    this->_is_empty = true;
                }

                return item;
            }

            /** Move up to @p max_items rows into a caller-owned batch buffer. */
            size_t drain_front(std::vector<T>& out, size_t max_items) {
                const size_t drain_count = this->records_.size() < max_items ? this->records_.size() : max_items;
                out.reserve(out.size() + drain_count);

                for (size_t i = 0; i < drain_count; ++i) {
                    out.push_back(std::move(this->records_.front()));
                    this->records_.pop_front();
                }

                if (this->records_.empty()) {
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
                return this->records_.size();
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
            std::deque<T> records_;
        };
    }
}
