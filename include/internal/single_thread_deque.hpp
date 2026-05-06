/** @file
 *  @brief Single-threaded row deque implementation
 */

#pragma once

#include <deque>
#include <utility>
#include <vector>

#include "row_queue_batch.hpp"
#include "row_queue_inspection.hpp"

namespace csv {
    namespace internals {
        template<typename T>
        class SingleThreadDeque {
        public:
            SingleThreadDeque(size_t notify_size = 100) : _notify_size(notify_size) {}

            SingleThreadDeque(const SingleThreadDeque& other) {
                this->batches_ = other.batches_;
                this->front_index_ = other.front_index_;
                this->size_ = other.size_;
                this->_notify_size = other._notify_size;
                this->_is_empty = other._is_empty;
                this->_is_waitable = other._is_waitable;
            }

            SingleThreadDeque(const std::deque<T>& source) : SingleThreadDeque() {
                std::vector<T> rows;
                rows.reserve(source.size());
                for (const auto& row : source) {
                    rows.push_back(row);
                }
                if (!rows.empty()) {
                    this->batches_.push_back(std::move(rows));
                    this->size_ = source.size();
                }
                this->_is_empty = source.empty();
            }

            bool empty() const noexcept {
                return this->_is_empty;
            }

            void push_back(T&& item) {
                std::vector<T> batch;
                batch.push_back(std::move(item));
                this->batches_.push_back(std::move(batch));
                this->size_++;
                this->_is_empty = false;
            }

            void append_rows(std::vector<T>&& rows) {
                if (rows.empty()) {
                    return;
                }

                this->size_ += rows.size();
                this->batches_.push_back(std::move(rows));
                this->_is_empty = false;
            }

            T pop_front() noexcept {
                T item = std::move(this->batches_.front()[this->front_index_]);
                this->front_index_++;
                this->size_--;
                this->discard_exhausted_front_batch();

                if (this->size_ == 0) {
                    this->_is_empty = true;
                }

                return item;
            }

            /** Move up to @p max_items rows into a caller-owned batch buffer. */
            size_t drain_front(std::vector<T>& out, size_t max_items) {
                const size_t drain_count = drain_front_batches(
                    this->batches_,
                    this->front_index_,
                    this->size_,
                    out,
                    max_items
                );

                if (this->size_ == 0) {
                    this->_is_empty = true;
                }

                return drain_count;
            }

            /** Invoke @p callback with a stable const view of queued rows.
             *
             *  Kept API-compatible with ThreadSafeDeque for diagnostics and tests.
             */
            template<typename Callback>
            void inspect(Callback&& callback) const {
                RowQueueInspectionView<T> view(this->batches_, this->front_index_, this->size_);
                std::forward<Callback>(callback)(view);
            }

            bool is_waitable() const noexcept {
                return this->_is_waitable;
            }

            void wait() {
                // No-op in single-thread mode.
            }

            size_t size() const noexcept {
                return this->size_;
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
