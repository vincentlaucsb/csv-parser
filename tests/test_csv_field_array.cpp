#include <catch2/catch_all.hpp>
#include <future>
#include <utility>

#include "csv.hpp"
#include "internal/memory/field_scalar_list.hpp"

using namespace csv;
using namespace csv::internals;

TEST_CASE("Test Dynamic RawCSVFieldArray - Emplace Back", "[test_dynamic_array_emplace]") {
    using namespace csv::internals;

    constexpr size_t offset = 100;

    // Array size should be smaller than the number of items we want to push
    RawCSVFieldList arr(500);

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

TEST_CASE("RawCSVFieldList preserves metadata across small block boundaries", "[raw_csv_field_list]") {
    RawCSVFieldList fields(3);

    for (size_t i = 0; i < 10; ++i) {
        fields.emplace_back(i * 10, i + 1, i % 2 == 0);
    }

    REQUIRE(fields.size() == 10);

    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];
        REQUIRE(field.start == i * 10);
        REQUIRE(field.length == i + 1);
        REQUIRE(field.has_realized_storage() == (i % 2 == 0));
    }
}

TEST_CASE("RawCSVFieldList treats zero block capacity as one", "[raw_csv_field_list]") {
    RawCSVFieldList fields(0);

    fields.emplace_back(7, 11, false);
    fields.emplace_back(13, 17, true);
    fields.emplace_back(19, 23, false);

    REQUIRE(fields.size() == 3);
    REQUIRE(fields[0].start == 7);
    REQUIRE(fields[0].length == 11);
    REQUIRE_FALSE(fields[0].has_realized_storage());
    REQUIRE(fields[1].start == 13);
    REQUIRE(fields[1].length == 17);
    REQUIRE(fields[1].has_realized_storage());
    REQUIRE(fields[2].start == 19);
    REQUIRE(fields[2].length == 23);
    REQUIRE_FALSE(fields[2].has_realized_storage());
}

TEST_CASE("RawCSVFieldList move keeps allocated field blocks stable", "[raw_csv_field_list]") {
    RawCSVFieldList original(2);

    for (size_t i = 0; i < 7; ++i) {
        original.emplace_back(i + 100, i + 200, i == 3);
    }

    RawCSVFieldList moved(std::move(original));

    REQUIRE(original.size() == 0);
    REQUIRE(moved.size() == 7);

    for (size_t i = 0; i < moved.size(); ++i) {
        const auto& field = moved[i];
        REQUIRE(field.start == i + 100);
        REQUIRE(field.length == i + 200);
        REQUIRE(field.has_realized_storage() == (i == 3));
    }
}

TEST_CASE("RawCSVFieldList reserve_for_source_size can grow pointer table without changing size", "[raw_csv_field_list]") {
    const size_t large_block_capacity = internals::CSV_CHUNK_SIZE_DEFAULT + 2;
    RawCSVFieldList fields(large_block_capacity);

    fields.reserve_for_source_size(large_block_capacity * 2);

    REQUIRE(fields.size() == 0);

    fields.emplace_back(1, 2, true);

    REQUIRE(fields.size() == 1);
    REQUIRE(fields[0].start == 1);
    REQUIRE(fields[0].length == 2);
    REQUIRE(fields[0].has_realized_storage());
}

TEST_CASE("CSVFieldScalarList keeps scalar values stable across block growth", "[test_scalar_list]") {
    memory::CSVFieldScalarList scalars(3);

    for (size_t i = 0; i < 100; ++i) {
        CSVFieldScalar scalar;
        scalar.type = DataType::CSV_INT64;
        scalar.integer = static_cast<std::int64_t>(i * 10);
        scalars.emplace_back(scalar);

        REQUIRE(scalars.size() == i + 1);
        REQUIRE(scalars[i].type == DataType::CSV_INT64);
        REQUIRE(scalars[i].integer == static_cast<std::int64_t>(i * 10));
    }

    for (size_t i = 0; i < 100; ++i) {
        REQUIRE(scalars[i].type == DataType::CSV_INT64);
        REQUIRE(scalars[i].integer == static_cast<std::int64_t>(i * 10));
    }
}

TEST_CASE("Test CSVFieldArray Thread Safety", "[test_array_thread]") {
    constexpr size_t offset = 100;
    constexpr size_t total_items = 9999;

    // Array size should be smaller than the number of items we want to push
    RawCSVFieldList arr(500);

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
    // Without these, the test could pass even if RawCSVFieldList is completely broken.
    
    constexpr size_t num_workers = 4;
    constexpr size_t chunk_size = total_items / num_workers;
    std::vector<std::future<bool>> workers = {};

    for (size_t i = 0; i < num_workers; i++) {
        size_t start = i * chunk_size;
        size_t end = (i == num_workers - 1) ? total_items : start + chunk_size;
        
        workers.push_back(
            std::async([](const RawCSVFieldList& arr, size_t start, size_t end, size_t offset) {
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
