/** @file
 *  @brief Timeout helper for race condition and stress tests
 *  
 *  Prevents hanging tests when deadlock regression occurs.
 *  Ensures explicit failure instead of CI timeout.
 */

#pragma once

#include <future>
#include <chrono>
#include <stdexcept>
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
 *  @throws std::runtime_error if timeout occurs
 *  @rethrows any exception thrown by fn
 */
template<typename Func, typename Duration = std::chrono::seconds>
void test_with_timeout(Func fn, Duration timeout = std::chrono::seconds(10)) {
    auto future = std::async(std::launch::async, fn);
    
    auto status = future.wait_for(timeout);
    
    REQUIRE(status == std::future_status::ready);
    
    // Re-throw any exception from the test function
    future.get();
}
