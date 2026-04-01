/** @file
 *  @brief Timeout helper for race condition and stress tests
 *  
 *  Prevents hanging tests when deadlock regression occurs.
 *  Ensures explicit failure instead of CI timeout.
 */

#pragma once

#include <future>
#include <thread>
#include <memory>
#include <chrono>
#include <catch2/catch_all.hpp>

/** Execute a test function with a timeout
 *  
 *  Usage:
 *  \code
 *  TEST_CASE("Race condition test", "[threading][race_condition]") {
 *      test_with_timeout([]() {
 *          for (int i = 0; i < 200; i++) {
 *              // ... test code that might deadlock ...
 *          }
 *      }, std::chrono::seconds(10));
 *  }
 *  \endcode
 *  
 *  @tparam Func Callable that takes no arguments and returns void
 *  @tparam Duration std::chrono duration type (default: seconds)
 *  @param fn Test function to execute
 *  @param timeout Maximum time to wait before failing (default: 10 seconds)
 *  
 *  @note On timeout, this helper fails the test via REQUIRE and does not join
 *        the worker thread. This avoids deadlocking the test thread while
 *        reporting a deterministic failure.
 *  @rethrows any exception thrown by fn (re-raised on the caller thread)
 */
template<typename Func, typename Duration = std::chrono::seconds>
void test_with_timeout(Func fn, Duration timeout = std::chrono::seconds(10)) {
    auto completion = std::make_shared<std::promise<void>>();
    auto future = completion->get_future();
    auto worker_exception = std::make_shared<std::exception_ptr>();

    std::thread([fn = std::move(fn), completion, worker_exception]() mutable {
        try {
            fn();
        }
        catch (...) {
            *worker_exception = std::current_exception();
        }

        try {
            completion->set_value();
        }
        catch (...) {
            // Promise may be abandoned on test shutdown paths.
        }
    }).detach();

    auto status = future.wait_for(timeout);
    REQUIRE(status == std::future_status::ready);

    if (*worker_exception) {
        std::rethrow_exception(*worker_exception);
    }
}
