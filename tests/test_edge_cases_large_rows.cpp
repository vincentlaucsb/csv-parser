//
// Tests for edge cases with large CSV rows
// Issue #218: Infinite loop when a row exceeds ITERATION_CHUNK_SIZE
//

#include <catch2/catch_all.hpp>
#include <sstream>
#include "csv.hpp"

using namespace csv;

/**
 * Generate a CSV row string of at least target_bytes (plus a trailing newline).
 * Each field is a fixed-size block of 'X' characters so the total payload is
 * predictable regardless of how large target_bytes is.
 */
std::string generate_large_row(size_t target_bytes, int num_fields = 10) {
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

TEST_CASE("Edge case: CSV rows larger than default chunk size", "[edge_cases_large_rows]") {
    
    SECTION("Normal row (smaller than default chunk)") {
        // Default chunk size is 10MB, this row is just 1KB
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
    
    SECTION("Exception thrown for row exceeding default chunk size (without custom size)") {
        // The infinite-loop detection fires when a full read-chunk is consumed
        // without producing any complete rows.  That requires the row to be
        // *strictly larger* than 2 × ITERATION_CHUNK_SIZE (2 × 10 MB = 20 MB).
        // A 25 MB row guarantees the second 10 MB chunk also contains no '\n'.
        std::stringstream ss;
        ss << "Col1,Col2,Col3,Col4,Col5\n";  // Header

        size_t row_size = 25 * 1024 * 1024;  // 25 MB — spans three 10 MB chunks
        ss << generate_large_row(row_size, 5);

        CSVReader reader(ss);

        // Attempting to iterate should throw exception about chunk size
        REQUIRE_THROWS_WITH(
            [&reader]() {
                for (auto& row : reader) {
                    (void)row;  // Use row to avoid unused warning
                }
            }(),
            Catch::Matchers::ContainsSubstring("chunk size")
        );
    }
    
    SECTION("Custom chunk size allows parsing larger rows") {
        // Row is 25 MB; with a 30 MB chunk it fits in a single read.
        std::stringstream ss;
        ss << "Col1,Col2,Col3\n";  // Header

        size_t row_size = 25 * 1024 * 1024;  // 25 MB
        ss << generate_large_row(row_size, 3);
        ss << "8,9,10\n";  // Another smaller row after

        CSVFormat fmt;
        fmt.delimiter(',').chunk_size(30 * 1024 * 1024);  // 30 MB
        CSVReader reader(ss, fmt);

        // Should now successfully parse without exception
        int row_count = 0;
        for (auto& row : reader) {
            row_count++;
            REQUIRE(row.size() == 3);  // Each row should have 3 columns
        }
        REQUIRE(row_count == 2);  // 2 data rows
    }
    
    SECTION("Multiple large rows with custom chunk size") {
        std::stringstream ss;
        ss << "A,B,C\n";  // Header

        // Each row is 25 MB; chunk is 30 MB so each row fits in one read.
        size_t row_size = 25 * 1024 * 1024;
        for (int i = 0; i < 3; ++i) {
            ss << generate_large_row(row_size, 3);
        }

        CSVFormat fmt;
        fmt.delimiter(',').chunk_size(30 * 1024 * 1024);  // 30 MB chunks
        CSVReader reader(ss, fmt);

        int row_count = 0;
        for (auto& row : reader) {
            row_count++;
            REQUIRE(row.size() == 3);
        }
        REQUIRE(row_count == 3);
    }
    
    SECTION("Invalid chunk size (less than minimum) throws exception") {
        // CSVFormat::chunk_size() validates at the point of configuration
        CSVFormat fmt;
        REQUIRE_THROWS_WITH(
            fmt.chunk_size(1024 * 1024),  // 1 MB — too small
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

        // Setting to the minimum should not throw and should parse correctly
        CSVFormat fmt;
        fmt.delimiter(',').chunk_size(10 * 1024 * 1024);  // Exactly 10 MB minimum
        CSVReader reader(ss, fmt);

        int row_count = 0;
        for (auto& row : reader) { (void)row; row_count++; }
        REQUIRE(row_count == 1);
    }
    
    SECTION("Custom chunk size persists across reads") {
        std::stringstream ss1, ss2;

        // 25 MB rows: reader1 uses a 30 MB chunk (succeeds),
        // reader2 uses the default 10 MB chunk (triggers the exception).
        size_t row_size = 25 * 1024 * 1024;

        ss1 << "X,Y,Z\n";
        ss1 << generate_large_row(row_size, 3);

        ss2 << "P,Q,R\n";
        ss2 << generate_large_row(row_size, 3);

        CSVFormat big_chunk;
        big_chunk.delimiter(',').chunk_size(30 * 1024 * 1024);
        CSVReader reader1(ss1, big_chunk);

        // reader2 uses the default CSVFormat (10 MB chunk) — should throw
        CSVReader reader2(ss2);

        // reader1 should work (30 MB chunk)
        int count1 = 0;
        for (auto& row : reader1) { (void)row; count1++; }
        REQUIRE(count1 == 1);

        // reader2 should fail (default 10 MB chunk)
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
        // the infinite-loop guard at csv_reader.cpp:331 fires.
        std::stringstream ss;
        ss << "A,B\n";  // Header
        ss << "1,2\n";  // Small row (parsed successfully)

        size_t large_row_size = 25 * 1024 * 1024;  // 25 MB — exceeds 2 × 10 MB
        ss << generate_large_row(large_row_size, 2);

        CSVReader reader(ss);
        // Default 10 MB chunk — use CSVFormat::chunk_size() to increase if needed

        // Header and first data row parse fine
        auto it = reader.begin();  // Points to "1,2"
        REQUIRE(it != reader.end());

        // First advance tries to read the next chunk for the 25 MB row;
        // chunk 2 finishes with no complete rows and _read_requested already
        // true → the infinite-loop guard fires.
        REQUIRE_THROWS_WITH(
            ++it,
            Catch::Matchers::ContainsSubstring("End of file not reached")
        );
    }
}
