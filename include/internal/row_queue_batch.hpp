/** @file
 *  @brief Shared helpers for batch-backed row queues.
 */

#pragma once

#include <cstddef>
#include <deque>
#include <iterator>
#include <vector>

namespace csv {
    namespace internals {
        template<typename T>
        size_t drain_front_batches(
            std::deque<std::vector<T>>& batches,
            size_t& front_index,
            size_t& size,
            std::vector<T>& out,
            size_t max_items
        ) {
            const size_t drain_count = size < max_items ? size : max_items;
            size_t remaining = drain_count;

            out.reserve(out.size() + drain_count);

            while (remaining > 0) {
                auto& batch = batches.front();
                const size_t available = batch.size() - front_index;
                const size_t take = available < remaining ? available : remaining;
                const auto first_offset = static_cast<typename std::vector<T>::difference_type>(front_index);
                const auto take_offset = static_cast<typename std::vector<T>::difference_type>(take);
                auto first = batch.begin() + first_offset;
                auto last = first + take_offset;

                out.insert(
                    out.end(),
                    std::make_move_iterator(first),
                    std::make_move_iterator(last)
                );

                front_index += take;
                size -= take;
                remaining -= take;

                while (!batches.empty() && front_index >= batches.front().size()) {
                    batches.pop_front();
                    front_index = 0;
                }
            }

            return drain_count;
        }
    }
}
