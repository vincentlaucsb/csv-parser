#include <catch2/catch_all.hpp>
#include <future>

#include "csv.hpp"

using namespace csv;
using namespace csv::internals;

TEST_CASE("Test Dynamic RawCSVFieldArray - Emplace Back", "[test_dynamic_array_emplace]") {
    using namespace csv::internals;

    constexpr size_t offset = 100;

    // Array size should be smaller than the number of items we want to push
    CSVFieldList arr(500);

    for (size_t i = 0; i < 9999; i++) {
        arr.emplace_back(i, i + offset);

        // Check operator[] as field was just populated
        REQUIRE(arr[i].start == i);
        REQUIRE(arr[i].length == i + offset);

        REQUIRE(arr.size() == i + 1);
    }

    for (size_t i = 0; i < 9999; i++) {
        // Check for potential data corruption
        REQUIRE(arr[i].start == i);
        REQUIRE(arr[i].length == i + offset);
    }
}

TEST_CASE("Test CSVFieldArray Thread Safety", "[test_array_thread]") {
    constexpr size_t offset = 100;
    constexpr size_t total_items = 9999;

    // Array size should be smaller than the number of items we want to push
    CSVFieldList arr(500);

    // Write all data in main thread
    for (size_t i = 0; i < total_items; i++) {
        arr.emplace_back(i, i + offset);
    }

    // Now verify contents from multiple reader threads simultaneously
    // This tests that concurrent reads from multiple threads are safe.
    // 
    // IMPORTANT: To avoid trivial tests that pass without actually testing anything,
    // ensure that:
    // 1. The data written is correct (each element has non-zero, distinct values)
    // 2. The readers verify actual data, not just empty states
    // 3. Multiple threads run concurrently, not sequentially
    // Without these, the test could pass even if CSVFieldList is completely broken.
    
    constexpr size_t num_workers = 4;
    constexpr size_t chunk_size = total_items / num_workers;
    std::vector<std::future<bool>> workers = {};

    for (size_t i = 0; i < num_workers; i++) {
        size_t start = i * chunk_size;
        size_t end = (i == num_workers - 1) ? total_items : start + chunk_size;
        
        workers.push_back(
            std::async([](const CSVFieldList& arr, size_t start, size_t end, size_t offset) {
                for (size_t i = start; i < end; i++) {
                    // Verify non-zero field lengths to catch trivial tests
                    if (arr[i].length == 0)
                        return false;
                    
                    if (arr[i].start != i || arr[i].length != i + offset)
                        return false;
                }
                return true;
            }, std::ref(arr), start, end, offset)
        );
    }

    // Verify all concurrent readers got correct data
    for (auto& result : workers) {
        REQUIRE(result.get() == true);
    }
}
