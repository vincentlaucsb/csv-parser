#include "csv.hpp"
#include "catch.hpp"
using namespace csv;

TEST_CASE("Test Dynamic RawCSVFieldArray", "[test_dynamic_array]") {
    using namespace csv::internals;

    constexpr size_t offset = 100;

    // Array size should be smaller than the number of items we want to push
    CSVFieldArray arr(500);

    for (size_t i = 0; i < 9999; i++) {
        arr.push_back({ i, i + offset });

        // Check operator[] as field was just populated
        REQUIRE(arr[i].start == i);
        REQUIRE(arr[i].length == i + offset );

        REQUIRE(arr.size() == i + 1);
    }

    for (size_t i = 0; i < 9999; i++) {
        // Check for potential data corruption
        REQUIRE(arr[i].start == i);
        REQUIRE(arr[i].length == i + offset);
    }
}

TEST_CASE("Test Dynamic RawCSVFieldArray - Emplace Back", "[test_dynamic_array_emplace]") {
    using namespace csv::internals;

    constexpr size_t offset = 100;

    // Array size should be smaller than the number of items we want to push
    CSVFieldArray arr(500);

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