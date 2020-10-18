#include <future>

#include "csv.hpp"
#include "catch.hpp"

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

    // Array size should be smaller than the number of items we want to push
    CSVFieldList arr(500);

    for (size_t i = 0; i < 9999; i++) {
        arr.emplace_back(i, i + offset);

        // Check operator[] as field was just populated
        REQUIRE(arr[i].start == i);
        REQUIRE(arr[i].length == i + offset);

        REQUIRE(arr.size() == i + 1);
    }

    // Check contents from another thread
    constexpr size_t num_workers = 4;
    constexpr size_t chunk_size = 9999 / num_workers;
    std::vector<std::future<bool>> workers = {};

    for (size_t i = 0; i < num_workers; i++) {
        size_t start = i * chunk_size;
        size_t end = start + chunk_size;
        
        workers.push_back(
            std::async([](const CSVFieldList& arr, size_t start, size_t end, size_t offset) {
                for (size_t i = start; i < end; i++) {
                    if (arr[i].start != i || arr[i].length != i + offset)
                        return false;
                }

                return true;
            }, std::ref(arr), start, end, offset)
        );
    }

    // Writer from another thread
    for (size_t i = 9999; i < 19999; i++) {
        arr.emplace_back(i, i + offset);

        // Check operator[] as field was just populated
        REQUIRE(arr[i].start == i);
        REQUIRE(arr[i].length == i + offset);

        REQUIRE(arr.size() == i + 1);
    }

    for (auto& result : workers) {
        REQUIRE(result.get() == true);
    }
}
