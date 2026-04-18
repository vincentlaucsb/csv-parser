/** @file
 *  @brief Shared contracts for row deque implementations
 */

#pragma once

#include "common.hpp"

#if CSV_ENABLE_THREADS
#include "thread_safe_deque.hpp"
#else
#include "single_thread_deque.hpp"
#endif

#include <cstddef>
#include <type_traits>
#include <utility>

#ifdef CSV_HAS_CXX20
#include <concepts>
#endif

namespace csv {
    namespace internals {
#if !CSV_ENABLE_THREADS
    template<typename T>
    using ThreadSafeDeque = SingleThreadDeque<T>;
#endif

#ifdef CSV_HAS_CXX20
        template<typename Q, typename T>
        concept RowDequeLike = requires(Q q, const Q cq, T item, size_t n) {
            { Q(100) };
            { q.push_back(std::move(item)) } -> std::same_as<void>;
            { q.pop_front() } -> std::same_as<T>;
            { cq.empty() } -> std::same_as<bool>;
            { cq.is_waitable() } -> std::same_as<bool>;
            { q.wait() } -> std::same_as<void>;
            { q.notify_all() } -> std::same_as<void>;
            { q.kill_all() } -> std::same_as<void>;
            { q.front() } -> std::same_as<T&>;
            { q[n] } -> std::same_as<T&>;
            { cq.size() } -> std::same_as<size_t>;
            { q.begin() };
            { q.end() };
        };

        #if CSV_ENABLE_THREADS
            static_assert(RowDequeLike<ThreadSafeDeque<int>, int>, "ThreadSafeDeque must satisfy RowDequeLike contract");
        #else
            static_assert(RowDequeLike<SingleThreadDeque<int>, int>, "SingleThreadDeque must satisfy RowDequeLike contract");
            static_assert(RowDequeLike<ThreadSafeDeque<int>, int>, "Selected ThreadSafeDeque alias must satisfy RowDequeLike contract");
        #endif
#endif
    }
}
