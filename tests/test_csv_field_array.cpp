#include "csv.hpp"
#include "catch.hpp"
using namespace csv;

TEST_CASE("Test Dynamic RawCSVFieldArray", "[test_dynamic_array]") {
    using namespace csv::internals;

    constexpr size_t offset = 100;

    CSVFieldArray arr;
    for (size_t i = 0; i < 9999; i++) {
        arr.push_back({ i, i + offset });
        REQUIRE(arr.size() == i + 1);
    }

    for (size_t i = 0; i < 9999; i++) {
        REQUIRE(arr[i].start == i);
        REQUIRE(arr[i].length == i + offset);
    }
}