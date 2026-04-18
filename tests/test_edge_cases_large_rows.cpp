//
// Tests for edge cases with large CSV rows
// Issue #218: Infinite loop when a row exceeds CSV_CHUNK_SIZE_DEFAULT
//

#include <catch2/catch_all.hpp>
#include <fstream>
#include <sstream>
#include "csv.hpp"
#include "shared/file_guard.hpp"

using namespace csv;

#ifndef __EMSCRIPTEN__
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

constexpr size_t kRowBytesStress = 25 * 1024 * 1024;
// StreamParser prepends leftover bytes from the previous read, so the second
// parse attempt can process nearly 2x the default chunk. Keep a margin above
// that to reliably hit the infinite-loop guard path.
constexpr size_t kRowBytesGuardTrip = 2 * internals::CSV_CHUNK_SIZE_DEFAULT + 2 * 1024 * 1024;
constexpr size_t kRowBytesMedium = internals::CSV_CHUNK_SIZE_DEFAULT + 64 * 1024;
constexpr size_t kChunkBytesMedium = internals::CSV_CHUNK_SIZE_DEFAULT + 256 * 1024;

// Build shared rows once. Catch2 re-runs the preamble before each SECTION,
// so static locals avoid repeated multi-megabyte heap allocations.
static const std::string& stress_row_3col() {
    static const std::string row = generate_large_row(kRowBytesStress, 3);
    return row;
}

static const std::string& guard_trip_row_3col() {
    static const std::string row = generate_large_row(kRowBytesGuardTrip, 3);
    return row;
}

static const std::string& guard_trip_row_2col() {
    static const std::string row = generate_large_row(kRowBytesGuardTrip, 2);
    return row;
}

static const std::string& medium_row_3col() {
    static const std::string row = generate_large_row(kRowBytesMedium, 3);
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
        // producing any complete rows. StreamParser carries leftover bytes
        // across reads, so this row includes a safety margin above 2x default.
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
            ss << "Col1,Col2,Col3\n" << guard_trip_row_3col();
            CSVReader reader(ss);
            validate_throws(reader);
        }

        #ifndef __EMSCRIPTEN__
        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_large_row_throw.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << "Col1,Col2,Col3\n" << guard_trip_row_3col();
            }
            CSVReader reader(cleanup.filename);
            validate_throws(reader);
        }
        #endif
    }

    SECTION("Custom chunk size allows parsing larger rows") {
        // Row is just above default chunk size; medium chunk size should parse it.
        CSVFormat fmt;
        fmt.delimiter(',').chunk_size(kChunkBytesMedium);

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
            ss << "Col1,Col2,Col3\n" << medium_row_3col() << "8,9,10\n";
            CSVReader reader(ss, fmt);
            validate_reader(reader);
        }

        #ifndef __EMSCRIPTEN__
        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_large_row_parse.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << "Col1,Col2,Col3\n" << medium_row_3col() << "8,9,10\n";
            }
            CSVReader reader(cleanup.filename, fmt);
            validate_reader(reader);
        }
        #endif
    }

    SECTION("Single 25MB row stress test with custom chunk size") {
        // Keep one explicit 25 MB stress path for throughput/regression coverage.
        CSVFormat fmt;
        fmt.delimiter(',').chunk_size(30 * 1024 * 1024);

        auto validate_reader = [](CSVReader& reader) {
            int row_count = 0;
            for (auto& row : reader) {
                row_count++;
                REQUIRE(row.size() == 3);
            }
            REQUIRE(row_count == 1);
        };

        SECTION("stream path") {
            std::stringstream ss;
            ss << "A,B,C\n" << stress_row_3col();
            CSVReader reader(ss, fmt);
            validate_reader(reader);
        }

        #ifndef __EMSCRIPTEN__
        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_large_rows_multiple.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << "A,B,C\n" << stress_row_3col();
            }
            CSVReader reader(cleanup.filename, fmt);
            validate_reader(reader);
        }
        #endif
    }

    SECTION("Invalid chunk size (less than minimum) throws exception") {
        // CSVFormat::chunk_size() validates at the point of configuration.
        CSVFormat fmt;
        REQUIRE_THROWS_WITH(
            fmt.chunk_size(128 * 1024),  // 128 KB — below the 500 KB minimum
            Catch::Matchers::ContainsSubstring("at least")
        );
        REQUIRE_THROWS_WITH(
            fmt.chunk_size(0),
            Catch::Matchers::ContainsSubstring("at least")
        );
    }

    SECTION("Minimum allowed chunk size (exactly CSV_CHUNK_SIZE_FLOOR) works") {
        std::stringstream ss;
        ss << "A,B\n1,2\n";

        CSVFormat fmt;
        fmt.delimiter(',').chunk_size(internals::CSV_CHUNK_SIZE_FLOOR);  // Exactly 500 KB minimum
        CSVReader reader(ss, fmt);

        int row_count = 0;
        for (auto& row : reader) { (void)row; row_count++; }
        REQUIRE(row_count == 1);
    }

    SECTION("Custom chunk size persists across reads") {
        // reader1 uses a medium chunk and medium row (succeeds);
        // reader2 uses the default chunk with a >2x default row + margin (throws).
        std::stringstream ss1, ss2;
        ss1 << "X,Y,Z\n" << medium_row_3col();
        ss2 << "P,Q,R\n" << guard_trip_row_3col();

        CSVFormat big_chunk;
        big_chunk.delimiter(',').chunk_size(kChunkBytesMedium);
        CSVReader reader1(ss1, big_chunk);
        CSVReader reader2(ss2);  // Default chunk size

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
        // A >2x default chunk row with margin spans three chunks; the second
        // chunk completes without finding a '\n', so _read_requested is already true →
        // the infinite-loop guard fires.
        std::stringstream ss;
        ss << "A,B\n" << "1,2\n" << guard_trip_row_2col();

        CSVReader reader(ss);

        // Header and first data row parse fine.
        auto it = reader.begin();  // Points to "1,2"
        REQUIRE(it != reader.end());

        // Advancing into the oversized row: chunk 2 finishes with no complete rows
        // and _read_requested is already true → the guard fires.
        REQUIRE_THROWS_WITH(
            ++it,
            Catch::Matchers::ContainsSubstring("End of file not reached")
        );
    }
}

// Verify parse_unsafe() (non-owning StringViewStream path) delivers correct values
// across the 10MB chunk boundary. Distinct per-column values (i*5+col) ensure that
// field corruption or mis-alignment at a chunk transition would be detected.
TEST_CASE("parse_unsafe() chunk boundary integrity", "[parse_unsafe_chunk_boundary]") {
    const size_t n_rows = 500000;

    std::string csv_data;
    csv_data.reserve(n_rows * 35);
    csv_data += "A,B,C,D,E\r\n";
    for (size_t i = 0; i < n_rows; i++) {
        csv_data += std::to_string(i * 5 + 0) + ','
                  + std::to_string(i * 5 + 1) + ','
                  + std::to_string(i * 5 + 2) + ','
                  + std::to_string(i * 5 + 3) + ','
                  + std::to_string(i * 5 + 4) + "\r\n";
    }

    // csv_data outlives the reader — the non-owning path is safe here.
    auto reader = parse_unsafe(csv_data);

    REQUIRE(reader.get_col_names() == std::vector<std::string>({"A", "B", "C", "D", "E"}));

    size_t i = 0;
    for (auto& row : reader) {
        REQUIRE(row.size() == 5);
        for (size_t col = 0; col < 5; col++) {
            REQUIRE(row[col].get<size_t>() == i * 5 + col);
        }
        i++;
    }
    REQUIRE(i == n_rows);
}
#endif
