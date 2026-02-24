//
// Tests for edge cases with large CSV rows
// Issue #218: Infinite loop when a row exceeds ITERATION_CHUNK_SIZE
//

#include <catch2/catch_all.hpp>
#include <fstream>
#include <sstream>
#include "csv.hpp"
#include "shared/file_guard.hpp"

using namespace csv;

/**
 * Generate a CSV row string of at least target_bytes (plus a trailing newline).
 * Each field is a fixed-size block of 'X' characters so the total payload is
 * predictable regardless of how large target_bytes is.
 */
static std::string generate_large_row(size_t target_bytes, int num_fields = 10) {
    // Distribute bytes evenly; last field absorbs any remainder
    size_t bytes_per_field = target_bytes / num_fields;
    size_t remainder       = target_bytes % num_fields;

    std::string row;
    row.reserve(target_bytes + num_fields + 1);

    for (int i = 0; i < num_fields; ++i) {
        if (i > 0) row += ",";
        size_t field_size = bytes_per_field + (i == num_fields - 1 ? remainder : 0);
        row += std::string(field_size, 'X');
    }
    row += "\n";
    return row;
}

// Build shared 25 MB rows once.  Catch2 re-runs the preamble before each
// SECTION, so static locals prevent repeated 25 MB heap allocations.
static const std::string& large_row_3col() {
    static const std::string row = generate_large_row(25 * 1024 * 1024, 3);
    return row;
}

static const std::string& large_row_2col() {
    static const std::string row = generate_large_row(25 * 1024 * 1024, 2);
    return row;
}

TEST_CASE("Edge case: CSV rows larger than default chunk size", "[edge_cases_large_rows]") {

    SECTION("Normal row (smaller than default chunk)") {
        // Default chunk size is 10 MB; this row is just a few bytes.
        auto csv_data = "A,B,C\n"
                        "1,2,3\n"
                        "4,5,6\n"_csv;

        int row_count = 0;
        for (auto& row : csv_data) {
            (void)row;
            row_count++;
        }
        REQUIRE(row_count == 2);
    }

    SECTION("Exception thrown for row exceeding default chunk size") {
        // The infinite-loop guard fires when a full chunk is consumed without
        // producing any complete rows.  This requires the row to be strictly
        // larger than 2 × ITERATION_CHUNK_SIZE (2 × 10 MB = 20 MB).
        // A 25 MB row guarantees the second 10 MB chunk also contains no '\n'.
        auto validate_throws = [](CSVReader& reader) {
            REQUIRE_THROWS_WITH(
                [&reader]() {
                    for (auto& row : reader) { (void)row; }
                }(),
                Catch::Matchers::ContainsSubstring("chunk size")
            );
        };

        SECTION("stream path") {
            std::stringstream ss;
            ss << "Col1,Col2,Col3\n" << large_row_3col();
            CSVReader reader(ss);
            validate_throws(reader);
        }

        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_large_row_throw.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << "Col1,Col2,Col3\n" << large_row_3col();
            }
            CSVReader reader(cleanup.filename);
            validate_throws(reader);
        }
    }

    SECTION("Custom chunk size allows parsing larger rows") {
        // Row is 25 MB; with a 30 MB chunk it fits in a single read.
        CSVFormat fmt;
        fmt.delimiter(',').chunk_size(30 * 1024 * 1024);

        auto validate_reader = [](CSVReader& reader) {
            int row_count = 0;
            for (auto& row : reader) {
                row_count++;
                REQUIRE(row.size() == 3);
            }
            REQUIRE(row_count == 2);
        };

        SECTION("stream path") {
            std::stringstream ss;
            ss << "Col1,Col2,Col3\n" << large_row_3col() << "8,9,10\n";
            CSVReader reader(ss, fmt);
            validate_reader(reader);
        }

        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_large_row_parse.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << "Col1,Col2,Col3\n" << large_row_3col() << "8,9,10\n";
            }
            CSVReader reader(cleanup.filename, fmt);
            validate_reader(reader);
        }
    }

    SECTION("Multiple large rows with custom chunk size") {
        // Each row is 25 MB; 30 MB chunk fits each row in a single read.
        CSVFormat fmt;
        fmt.delimiter(',').chunk_size(30 * 1024 * 1024);

        auto validate_reader = [](CSVReader& reader) {
            int row_count = 0;
            for (auto& row : reader) {
                row_count++;
                REQUIRE(row.size() == 3);
            }
            REQUIRE(row_count == 3);
        };

        SECTION("stream path") {
            std::stringstream ss;
            ss << "A,B,C\n";
            for (int i = 0; i < 3; ++i) ss << large_row_3col();
            CSVReader reader(ss, fmt);
            validate_reader(reader);
        }

        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_large_rows_multiple.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << "A,B,C\n";
                for (int i = 0; i < 3; ++i) out << large_row_3col();
            }
            CSVReader reader(cleanup.filename, fmt);
            validate_reader(reader);
        }
    }

    SECTION("Invalid chunk size (less than minimum) throws exception") {
        // CSVFormat::chunk_size() validates at the point of configuration.
        CSVFormat fmt;
        REQUIRE_THROWS_WITH(
            fmt.chunk_size(1024 * 1024),  // 1 MB — below the 10 MB minimum
            Catch::Matchers::ContainsSubstring("at least")
        );
        REQUIRE_THROWS_WITH(
            fmt.chunk_size(0),
            Catch::Matchers::ContainsSubstring("at least")
        );
    }

    SECTION("Minimum allowed chunk size (exactly ITERATION_CHUNK_SIZE) works") {
        std::stringstream ss;
        ss << "A,B\n1,2\n";

        CSVFormat fmt;
        fmt.delimiter(',').chunk_size(10 * 1024 * 1024);  // Exactly 10 MB minimum
        CSVReader reader(ss, fmt);

        int row_count = 0;
        for (auto& row : reader) { (void)row; row_count++; }
        REQUIRE(row_count == 1);
    }

    SECTION("Custom chunk size persists across reads") {
        // reader1 uses a 30 MB chunk (succeeds);
        // reader2 uses the default 10 MB chunk (triggers the exception).
        std::stringstream ss1, ss2;
        ss1 << "X,Y,Z\n" << large_row_3col();
        ss2 << "P,Q,R\n" << large_row_3col();

        CSVFormat big_chunk;
        big_chunk.delimiter(',').chunk_size(30 * 1024 * 1024);
        CSVReader reader1(ss1, big_chunk);
        CSVReader reader2(ss2);  // Default 10 MB chunk

        int count1 = 0;
        for (auto& row : reader1) { (void)row; count1++; }
        REQUIRE(count1 == 1);

        REQUIRE_THROWS(
            [&reader2]() {
                for (auto& row : reader2) { (void)row; }
            }()
        );
    }
}

TEST_CASE("Issue #218 - Infinite read loop detection", "[issue_218]") {

    SECTION("Detects when row exceeds chunk size and file doesn't end") {
        // A 25 MB row spans three 10 MB chunks; the second chunk completes
        // without finding a '\n', so _read_requested is already true →
        // the infinite-loop guard fires.
        std::stringstream ss;
        ss << "A,B\n" << "1,2\n" << large_row_2col();

        CSVReader reader(ss);

        // Header and first data row parse fine.
        auto it = reader.begin();  // Points to "1,2"
        REQUIRE(it != reader.end());

        // Advancing into the 25 MB row: chunk 2 finishes with no complete rows
        // and _read_requested is already true → the guard fires.
        REQUIRE_THROWS_WITH(
            ++it,
            Catch::Matchers::ContainsSubstring("End of file not reached")
        );
    }
}
