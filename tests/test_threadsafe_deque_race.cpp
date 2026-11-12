//
// Test for ThreadSafeDeque race condition fix
// Bug: kill_all() and notify_all() did not hold the mutex when modifying
// _is_waitable and signaling the condition variable. This caused a race
// between read_row()'s is_waitable() check and the worker thread's kill_all(),
// leading to permanent deadlock on small CSV files.
//

#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/generated_file.hpp"
#include "shared/timeout_helper.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>

using namespace csv;

namespace {
#ifndef __has_feature
#define CSV_TEST_HAS_FEATURE(x) 0
#else
#define CSV_TEST_HAS_FEATURE(x) __has_feature(x)
#endif

#if defined(__SANITIZE_THREAD__) || CSV_TEST_HAS_FEATURE(thread_sanitizer)
    constexpr int RAPID_SMALL_CSV_ITERATIONS = 150;
#else
    constexpr int RAPID_SMALL_CSV_ITERATIONS = 500;
#endif
}

#if CSV_ENABLE_THREADS && !defined(__EMSCRIPTEN__)
namespace {
    constexpr size_t SPECULATIVE_DIAGNOSTICS_RACE_ROWS = 500000;

    const std::string& speculative_diagnostics_race_filename() {
        static csv_test::GeneratedFile file("csv_reader_speculative_diagnostics_race.csv");
        return file.path([](std::ofstream& out) {
            out << "col_a,col_b,col_c\n" << std::setfill('0');
            for (size_t row = 0; row < SPECULATIVE_DIAGNOSTICS_RACE_ROWS; ++row) {
                // Fixed-width, distinct fields make the fixture exceed the 10 MB
                // parser boundary while retaining corruption-detecting values.
                out << std::setw(7) << row * 3 << ','
                    << std::setw(7) << row * 3 + 1 << ','
                    << std::setw(7) << row * 3 + 2 << '\n';
            }
        });
    }

    bool coherent_diagnostics(const internals::SpeculativeParseDiagnostics& diagnostics) {
        return diagnostics.assumed_quoted_chunks + diagnostics.assumed_unquoted_chunks
                == diagnostics.chunks
            && diagnostics.ambiguous_chunks <= diagnostics.chunks
            && diagnostics.probability_model_chunks <= diagnostics.ambiguous_chunks
            && diagnostics.record_size_heuristic_chunks <= diagnostics.ambiguous_chunks;
    }

    void exercise_speculative_diagnostics_race(
        CSVReader& reader,
        const std::shared_ptr<ThreadSafeErrorCollector>& errors
    ) {
        if (reader.parse_worker_count() != 2) {
            errors->add_error("parse_worker_count() != 2");
        }

        std::atomic<bool> stop_observer{ false };
        std::atomic<bool> observer_started{ false };
        bool observer_saw_incoherent_diagnostics = false;
        bool observer_saw_speculative_chunks = false;
        size_t previous_chunk_count = 0;

        std::thread observer([&]() {
            observer_started.store(true, std::memory_order_release);
            do {
                // Before issue #353 was fixed, this snapshot raced with merges
                // made by CSVReader's background parser. TSan reports that race
                // even when an ordinary run observes plausible counter values.
                const auto diagnostics = reader.speculative_diagnostics();

                if (!coherent_diagnostics(diagnostics)
                    || diagnostics.chunks < previous_chunk_count) {
                    observer_saw_incoherent_diagnostics = true;
                }
                observer_saw_speculative_chunks = observer_saw_speculative_chunks
                    || diagnostics.chunks > 0;
                previous_chunk_count = diagnostics.chunks;
                std::this_thread::yield();
            } while (!stop_observer.load(std::memory_order_acquire));
        });
        while (!observer_started.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        CSVError read_error;
        CSVRow row;
        size_t row_count = 0;
        while (reader.try_read_row(row, read_error)) {
            if (row.size() != 3) {
                errors->add_error("row.size() != 3");
                break;
            }

            // Sample rows periodically while keeping the TSan test affordable.
            if (row_count % 100000 == 0
                || row_count + 1 == SPECULATIVE_DIAGNOSTICS_RACE_ROWS) {
                size_t values[3] = {};
                for (size_t column = 0; column < 3; ++column) {
                    if (!row[column].try_get(values[column])) {
                        errors->add_error("Failed to convert sampled field");
                    }
                    const size_t expected = row_count * 3 + column;
                    if (values[column] != expected) {
                        errors->add_error(
                            "Sampled field mismatch at row " + std::to_string(row_count)
                                + ", column " + std::to_string(column)
                        );
                    }
                }
            }
            ++row_count;
        }

        stop_observer.store(true, std::memory_order_release);
        observer.join();

        if (read_error) {
            errors->add_error("CSV read failed: " + read_error.message());
        }
        if (row_count != SPECULATIVE_DIAGNOSTICS_RACE_ROWS) {
            errors->add_error(
                "row_count != 500000 (got " + std::to_string(row_count) + ")"
            );
        }

        const auto final_diagnostics = reader.speculative_diagnostics();
        if (!observer_saw_speculative_chunks || final_diagnostics.chunks == 0) {
            errors->add_error("Speculative parser was not exercised");
        }
        if (observer_saw_incoherent_diagnostics || !coherent_diagnostics(final_diagnostics)) {
            errors->add_error("Observed an incoherent speculative diagnostics snapshot");
        }
    }
}
#endif

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
          "[threading][race_condition][stress]") {
    // Stress test: rapidly create and iterate many small CSVs
    // to maximize the chance of hitting the race window
    SECTION("Rapid sequential small CSV parsing") {
        auto errors = std::make_shared<ThreadSafeErrorCollector>();
        test_with_timeout([errors]() {
            // ThreadSanitizer makes this tiny-reader loop dramatically slower.
            // Fewer iterations still exercise the race window while keeping CI
            // focused on actual race reports instead of instrumentation cost.
            for (int i = 0; i < RAPID_SMALL_CSV_ITERATIONS; i++) {
                auto rows = "X,Y\n1,2\n"_csv;
                CSVRow row;
                if (!rows.read_row(row)) errors->add_error("Failed to read first row");
                auto x_val = row["X"].get<int>();
                auto y_val = row["Y"].get<int>();
                if (x_val != 1) errors->add_error("X != 1 (got " + std::to_string(x_val) + ")");
                if (y_val != 2) errors->add_error("Y != 2 (got " + std::to_string(y_val) + ")");
                if (rows.read_row(row)) errors->add_error("read_row() should have returned false for second read");
            }
        }, std::chrono::seconds(60));
        errors->check_and_fail_if_errors();
    }
}

#if CSV_ENABLE_THREADS && !defined(__EMSCRIPTEN__)
TEST_CASE("CSVReader speculative diagnostics remain coherent during background parsing - Issue #353",
          "[threading][race_condition][issue_353]") {
    const std::string& filename = speculative_diagnostics_race_filename();

    CSVFormat format;
    format.delimiter(',')
        .header_row(0)
        .chunk_size(internals::CSV_CHUNK_SIZE_FLOOR)
        .speculative_parallel_threads(2)
        .speculative_parallel_min_bytes(0);

    auto errors = std::make_shared<ThreadSafeErrorCollector>();

    SECTION("Memory-mapped file path") {
        test_with_timeout([filename, format, errors]() {
            CSVReader reader(filename, format);
            exercise_speculative_diagnostics_race(reader, errors);
        }, std::chrono::seconds(240));
    }

    SECTION("std::ifstream path") {
        test_with_timeout([filename, format, errors]() {
            std::ifstream input(filename, std::ios::binary);
            CSVReader reader(input, format);
            exercise_speculative_diagnostics_race(reader, errors);
        }, std::chrono::seconds(240));
    }

    errors->check_and_fail_if_errors();
}
#endif
