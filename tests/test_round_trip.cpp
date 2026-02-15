/** 
 * Round Trip Tests - Write and Read Verification
 * 
 * These tests validate CSV writing and reading by:
 * - Writing data to a file
 * - Reading it back
 * - Verifying exact data integrity
 * 
 * IMPORTANT: When tests validate I/O behavior, they should test BOTH code paths:
 * - CSVReader(filename) → memory-mapped I/O (mmap)
 * - CSVReader(std::istream&) → stream-based reading
 * 
 * These are separate implementations that can fail independently (see issue #281).
 * Use Catch2 SECTION pattern with a validation lambda to test both paths efficiently.
 */

#include <array>
#include <catch2/catch_all.hpp>
#include <cstdio>
#include <iostream>

#include "csv.hpp"

using namespace csv;

// RAII helper to ensure test files are always cleaned up, even if REQUIRE fails
struct FileGuard {
    std::string filename;
    explicit FileGuard(std::string fname) : filename(std::move(fname)) {}
    ~FileGuard() { std::remove(filename.c_str()); }
    FileGuard(const FileGuard&) = delete;
    FileGuard& operator=(const FileGuard&) = delete;
};

TEST_CASE("Simple Buffered Integer Round Trip Test", "[test_roundtrip_int]") {
    auto filename = "round_trip.csv";
    FileGuard cleanup(filename);  // Ensures file is deleted even if test fails
    
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
        for (auto &col : row) {
            REQUIRE(col == i);
        }

        i++;
    }

    REQUIRE(reader.n_rows() == n_rows);
}

TEST_CASE("Simple Integer Round Trip Test", "[test_roundtrip_int]") {
    auto filename = "round_trip.csv";
    FileGuard cleanup(filename);  // Ensures file is deleted even if test fails
    
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
        for (auto& col : row) {
            REQUIRE(col == i);
        }

        i++;
    }

    REQUIRE(reader.n_rows() == n_rows);
}