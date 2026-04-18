/** @file
 *  @brief Timeout helper for race condition and stress tests
 *  
 *  Prevents hanging tests when deadlock regression occurs.
 *  Ensures explicit failure instead of CI timeout.
 *  
 *  IMPORTANT: Do NOT use REQUIRE/CHECK assertions from the worker thread,
 *  as Catch2 is not thread-safe for concurrent assertion calls.
 *  Instead, collect errors and return them for assertion in the main thread.
 */

#pragma once

#include <future>
#include <thread>
#include <memory>
#include <chrono>
#include <string>
#include <vector>
#include <catch2/catch_all.hpp>

/** Thread-safe error collector for worker threads */
class ThreadSafeErrorCollector {
public:
    /** Record an assertion failure. Thread-safe. */
    void add_error(const std::string& msg) {
        errors.push_back(msg);
    }

    /** Check if any errors were recorded and fail the test if so. */
    void check_and_fail_if_errors() {
        if (!errors.empty()) {
            std::string msg = "Worker thread assertion failures:\n";
            for (const auto& e : errors) {
                msg += "  - " + e + "\n";
            }
            FAIL(msg);
        }
    }

private:
    std::vector<std::string> errors;
};

/** Execute a test function with a timeout
 *  
 *  Usage (OLD PATTERN - DO NOT USE, thread-unsafe):
 *  \code
 *  test_with_timeout([]() {
 *      REQUIRE(condition);  // UNSAFE if in worker thread!
 *  });
 *  \endcode
 *  
 *  Usage (NEW PATTERN - thread-safe):
 *  \code
 *  auto errors = std::make_shared<ThreadSafeErrorCollector>();
 *  test_with_timeout([errors]() {
 *      if (!condition) errors->add_error("condition failed");
 *  });
 *  errors->check_and_fail_if_errors();  // Main thread does the assertion
 *  \endcode
 *  
 *  @tparam Func Callable that takes no arguments and returns void
 *  @tparam Duration std::chrono duration type (default: seconds)
 *  @param fn Test function to execute (should NOT contain REQUIRE/CHECK calls)
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
