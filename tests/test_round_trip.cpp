/** 
 * Round Trip Tests - Write and Read Verification
 * 
 * These tests stress the CSV writer and reader by:
 * - Writing data with various complexity levels
 * - Reading it back via multiple code paths (mmap and std::istream)
 * - Verifying exact data integrity
 * 
 * Tests are ordered from simple to complex to validate:
 * 1. Basic functionality with uniform data
 * 2. Field boundary preservation with distinct values
 * 3. Proper handling of CSV edge cases (quoted fields, embedded delimiters/newlines)
 * 
 * All tests cross the 10MB chunk boundary to stress the threading/chunking infrastructure.
 */

#include <array>
#include <catch2/catch_all.hpp>
#include <cstdio>
#include <iostream>

#include "csv.hpp"
#include "shared/file_guard.hpp"

using namespace csv;

// ==============================================================================
// EASY: Basic round trip with uniform values
// ==============================================================================

TEST_CASE("Simple Buffered Integer Round Trip Test", "[test_roundtrip_int]") {
    auto filename = "round_trip.csv";
    FileGuard cleanup(filename);
    
    std::ofstream outfile(filename, std::ios::binary);
    auto writer = make_csv_writer_buffered(outfile);

    writer << std::vector<std::string>({"A", "B", "C", "D", "E"});

    const size_t n_rows = 1000000;

    for (size_t i = 0; i < n_rows; i++) {
        auto str = internals::to_string(i);
        writer << std::array<csv::string_view, 5>({str, str, str, str, str});
    }
    writer.flush();

    CSVReader reader(filename);

    size_t i = 0;
    for (auto &row : reader) {
        // Verify field count (detects if field boundaries are corrupted)
        REQUIRE(row.size() == 5);
        
        for (auto &col : row) {
            REQUIRE(col == i);
            
            // Verify field doesn't contain corruption markers (newlines/commas)
            auto field_str = col.get_sv();
            REQUIRE(field_str.find('\n') == std::string::npos);
            REQUIRE(field_str.find(',') == std::string::npos);
        }

        i++;
    }

    REQUIRE(reader.n_rows() == n_rows);
}

TEST_CASE("Simple Integer Round Trip Test", "[test_roundtrip_int]") {
    auto filename = "round_trip.csv";
    FileGuard cleanup(filename);
    
    std::ofstream outfile(filename, std::ios::binary);
    auto writer = make_csv_writer(outfile);

    writer << std::vector<std::string>({ "A", "B", "C", "D", "E" });

    const size_t n_rows = 1000000;

    for (size_t i = 0; i < n_rows; i++) {
        auto str = internals::to_string(i);
        writer << std::array<csv::string_view, 5>({ str, str, str, str, str });
    }

    CSVReader reader(filename);

    size_t i = 0;
    for (auto& row : reader) {
        // Verify field count (detects if field boundaries are corrupted)
        REQUIRE(row.size() == 5);
        
        for (auto& col : row) {
            REQUIRE(col == i);
            
            // Verify field doesn't contain corruption markers (newlines/commas)
            auto field_str = col.get_sv();
            REQUIRE(field_str.find('\n') == std::string::npos);
            REQUIRE(field_str.find(',') == std::string::npos);
        }

        i++;
    }

    REQUIRE(reader.n_rows() == n_rows);
}

// ==============================================================================
// MEDIUM: Distinct values to detect cross-field corruption
// ==============================================================================

TEST_CASE("Round Trip with Distinct Field Values", "[test_roundtrip_distinct]") {
    // This test uses DIFFERENT values in each column to detect cross-field corruption
    auto filename = "round_trip_distinct.csv";
    FileGuard cleanup(filename);
    
    // Write the CSV file
    {
        std::ofstream outfile(filename, std::ios::binary);
        auto writer = make_csv_writer(outfile);

        writer << std::vector<std::string>({ "col_A", "col_B", "col_C", "col_D", "col_E" });

        const size_t n_rows = 500000;  // Enough to cross 10MB chunk boundary

        for (size_t i = 0; i < n_rows; i++) {
            // Each column gets a DISTINCT value so corruption is obvious
            auto a = internals::to_string(i * 5 + 0);
            auto b = internals::to_string(i * 5 + 1);
            auto c = internals::to_string(i * 5 + 2);
            auto d = internals::to_string(i * 5 + 3);
            auto e = internals::to_string(i * 5 + 4);
            writer << std::array<csv::string_view, 5>({ a, b, c, d, e });
        }
    }

    const size_t expected_rows = 500000;

    // Test validation logic (same for both mmap and stream)
    auto validate_reader = [&](CSVReader& reader) {
        size_t i = 0;
        for (auto& row : reader) {
            // Verify field count
            REQUIRE(row.size() == 5);
            
            // Verify each field has its expected DISTINCT value
            REQUIRE(row["col_A"].get<size_t>() == i * 5 + 0);
            REQUIRE(row["col_B"].get<size_t>() == i * 5 + 1);
            REQUIRE(row["col_C"].get<size_t>() == i * 5 + 2);
            REQUIRE(row["col_D"].get<size_t>() == i * 5 + 3);
            REQUIRE(row["col_E"].get<size_t>() == i * 5 + 4);
            
            // Verify no corruption markers in any field
            for (auto& col : row) {
                auto field_str = col.get_sv();
                REQUIRE(field_str.find('\n') == std::string::npos);
                REQUIRE(field_str.find(',') == std::string::npos);
            }

            i++;
        }
        REQUIRE(reader.n_rows() == expected_rows);
    };

    SECTION("Memory-mapped file path") {
        CSVReader reader(filename);
        validate_reader(reader);
    }

    SECTION("std::ifstream path (issue #281)") {
        // Issue #281 was specific to the stream constructor
        std::ifstream infile(filename, std::ios::binary);
        CSVReader reader(infile, CSVFormat());
        validate_reader(reader);
    }
}

// ==============================================================================
// HARD: Complex quoted fields with embedded delimiters, newlines, and quotes
// ==============================================================================

TEST_CASE("Round Trip with Quoted Fields and Edge Cases", "[test_roundtrip_quoted]") {
    // Stress test: quoted fields with embedded delimiters, newlines, and escaped quotes
    // This tests the parser's ability to handle complex quoting scenarios across chunk boundaries
    auto filename = "round_trip_quoted.csv";

    FileGuard cleanup(filename);
    // Write the CSV file with challenging content
    {
        std::ofstream outfile(filename, std::ios::binary);
        auto writer = make_csv_writer(outfile);

        writer << std::vector<std::string>({ "id", "with_comma", "with_newline", "with_quote", "empty" });

        const size_t n_rows = 300000;  // Enough to cross 10MB chunk boundary

        for (size_t i = 0; i < n_rows; i++) {
            // Create progressively challenging fields
            auto id = internals::to_string(i);

            // Field with embedded comma (requires quoting)
            auto with_comma = "value," + internals::to_string(i) + ",data";

            // Field with embedded newline (requires quoting)
            auto with_newline = "line1\nline2_" + internals::to_string(i);

            // Field with embedded quote (requires quoting and escaping)
            auto with_quote = "quoted\"value\"" + internals::to_string(i);

            // Empty field
            auto empty = std::string("");

            writer << std::array<std::string, 5>({ id, with_comma, with_newline, with_quote, empty });
        }
    }

    const size_t expected_rows = 300000;

    // Validation logic
    auto validate_reader = [&](CSVReader& reader) {
        size_t i = 0;
        for (auto& row : reader) {
            // Verify field count
            REQUIRE(row.size() == 5);

            // Verify id field
            REQUIRE(row["id"].get<size_t>() == i);

            // Verify field with embedded comma (should NOT be split!)
            auto expected_comma = "value," + internals::to_string(i) + ",data";
            REQUIRE(row["with_comma"].get<std::string>() == expected_comma);

            // Verify field with embedded newline (should be preserved!)
            auto expected_newline = "line1\nline2_" + internals::to_string(i);
            REQUIRE(row["with_newline"].get<std::string>() == expected_newline);

            // Verify field with escaped quotes (should be unescaped to single quotes)
            auto expected_quote = "quoted\"value\"" + internals::to_string(i);
            REQUIRE(row["with_quote"].get<std::string>() == expected_quote);

            // Verify empty field
            REQUIRE(row["empty"].get<std::string>() == "");

            i++;
        }
        REQUIRE(reader.n_rows() == expected_rows);
        };

    SECTION("Memory-mapped file path") {
        CSVReader reader(filename);
        validate_reader(reader);
    }

    SECTION("std::ifstream path") {
        std::ifstream infile(filename, std::ios::binary);
        CSVReader reader(infile, CSVFormat());
        validate_reader(reader);
    }
}
