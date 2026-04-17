//
// Test for ThreadSafeDeque race condition fix
// Bug: kill_all() and notify_all() did not hold the mutex when modifying
// _is_waitable and signaling the condition variable. This caused a race
// between read_row()'s is_waitable() check and the worker thread's kill_all(),
// leading to permanent deadlock on small CSV files.
//

#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/timeout_helper.hpp"
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>

using namespace csv;

TEST_CASE("ThreadSafeDeque kill_all race condition - small file iterator",
          "[threading][race_condition]") {
    // This test reproduces a race condition in ThreadSafeDeque where:
    //   1. read_row() checks is_waitable() == true  (without lock)
    //   2. Worker thread calls kill_all(): sets _is_waitable=false, notify_all()
    //   3. read_row() enters wait() -- but the notification was already sent
    //   4. DEADLOCK: wait() blocks forever because _is_waitable is already false
    //      but the condition variable was notified before wait() was entered.
    //
    // The fix: kill_all() and notify_all() must acquire the mutex before
    // modifying _is_waitable and signaling the condition variable.
    //
    // This race is timing-dependent and most likely to trigger with:
    //   - Very small CSV data (worker finishes almost instantly)
    //   - no_header() mode (fewer rows to process)
    //   - Repeated iterations over multiple small inputs

    SECTION("Iterator over small in-memory CSV with no_header") {
        auto errors = std::make_shared<ThreadSafeErrorCollector>();
        test_with_timeout([errors]() {
            // Run many iterations to increase the chance of hitting the race window
            for (int i = 0; i < 200; i++) {
                CSVFormat fmt;
                fmt.no_header();

                std::stringstream ss("1,2,3\n4,5,6\n");
                CSVReader reader(ss, fmt);

                int row_count = 0;
                for (auto& row : reader) {
                    row_count++;
                    if (row.size() != 3) errors->add_error("row.size() != 3");
                }
                if (row_count != 2) errors->add_error("row_count != 2 (got " + std::to_string(row_count) + ")");
            }
        });
        errors->check_and_fail_if_errors();
    }

    SECTION("read_row over small in-memory CSV with no_header") {
        auto errors = std::make_shared<ThreadSafeErrorCollector>();
        test_with_timeout([errors]() {
            for (int i = 0; i < 200; i++) {
                CSVFormat fmt;
                fmt.no_header();

                std::stringstream ss("a,b\nx,y\n");
                CSVReader reader(ss, fmt);

                CSVRow row;
                int row_count = 0;
                while (reader.read_row(row)) {
                    row_count++;
                    if (row.size() != 2) errors->add_error("row.size() != 2");
                }
                if (row_count != 2) errors->add_error("row_count != 2 (got " + std::to_string(row_count) + ")");
            }
        });
        errors->check_and_fail_if_errors();
    }

    SECTION("Iterator with header over tiny CSV") {
        auto errors = std::make_shared<ThreadSafeErrorCollector>();
        test_with_timeout([errors]() {
            for (int i = 0; i < 200; i++) {
                std::stringstream ss("col1,col2\nval1,val2\n");
                CSVReader reader(ss);

                int row_count = 0;
                for (auto& row : reader) {
                    row_count++;
                    auto val = row["col1"].get<>();
                    if (val != "val1") errors->add_error("col1 != 'val1' (got '" + val + "')");
                }
                if (row_count != 1) errors->add_error("row_count != 1 (got " + std::to_string(row_count) + ")");
            }
        });
        errors->check_and_fail_if_errors();
    }
}

TEST_CASE("ThreadSafeDeque concurrent stress test",
          "[threading][race_condition]") {
    // Stress test: rapidly create and iterate many small CSVs
    // to maximize the chance of hitting the race window
    SECTION("Rapid sequential small CSV parsing") {
        auto errors = std::make_shared<ThreadSafeErrorCollector>();
        test_with_timeout([errors]() {
            for (int i = 0; i < 500; i++) {
                auto rows = "X,Y\n1,2\n"_csv;
                CSVRow row;
                if (!rows.read_row(row)) errors->add_error("Failed to read first row");
                auto x_val = row["X"].get<int>();
                auto y_val = row["Y"].get<int>();
                if (x_val != 1) errors->add_error("X != 1 (got " + std::to_string(x_val) + ")");
                if (y_val != 2) errors->add_error("Y != 2 (got " + std::to_string(y_val) + ")");
                if (rows.read_row(row)) errors->add_error("read_row() should have returned false for second read");
            }
        });
        errors->check_and_fail_if_errors();
    }
}
