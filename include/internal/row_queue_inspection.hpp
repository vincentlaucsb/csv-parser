/** @file
 *  @brief Storage-neutral read-only inspection view for row queues.
 */

#pragma once

#include <cstddef>
#include <deque>
#include <stdexcept>
#include <vector>

namespace csv {
    namespace internals {
        template<typename T>
        class RowQueueInspectionView {
        public:
            RowQueueInspectionView(
                const std::deque<std::vector<T>>& batches,
                size_t front_index,
                size_t size
            ) noexcept
                : batches_(batches),
                  front_index_(front_index),
                  size_(size) {}

            size_t size() const noexcept {
                return this->size_;
            }

            const T& operator[](size_t index) const {
                return this->at(index);
            }

            const T& at(size_t index) const {
                if (index >= this->size_) {
                    throw std::out_of_range("Row queue inspection index out of range");
                }

                size_t offset = index;
                for (size_t batch_index = 0; batch_index < this->batches_.size(); ++batch_index) {
                    const auto& batch = this->batches_[batch_index];
                    const size_t start = batch_index == 0 ? this->front_index_ : 0;
                    const size_t available = batch.size() - start;
                    if (offset < available) {
                        return batch[start + offset];
                    }
                    offset -= available;
                }

                throw std::out_of_range("Row queue inspection index out of range");
            }

        private:
            const std::deque<std::vector<T>>& batches_;
            size_t front_index_;
            size_t size_;
        };
    }
}
