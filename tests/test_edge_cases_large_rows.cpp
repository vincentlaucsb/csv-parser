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
        CSVFormat serial_format;
        serial_format.threading(false);

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
            CSVReader reader(ss, serial_format);
            validate_throws(reader);
        }

        #ifndef __EMSCRIPTEN__
        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_large_row_throw.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << "Col1,Col2,Col3\n" << guard_trip_row_3col();
            }
            CSVReader reader(cleanup.filename, serial_format);
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
        CSVFormat serial_format;
        serial_format.threading(false);
        CSVReader reader1(ss1, big_chunk);
        CSVReader reader2(ss2, serial_format);  // Default chunk size, serial parser

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

TEST_CASE("Issue #337 - First row longer than the mmap head buffer", "[issue_337]") {
    // MmapParser parses its pre-read head buffer (capped at 500KB regardless of
    // chunk_size) as the first window. When that window held no complete row,
    // trim_header() ran against an empty queue, marked the header as trimmed,
    // and the reader silently produced zero columns and zero rows. The stream
    // path was unaffected because its first window is the head plus a full read.
    const size_t n_rows = 1000;
    std::string body;
    for (size_t i = 0; i < n_rows; ++i) {
        body += std::to_string(i * 3 + 0) + ','
              + std::to_string(i * 3 + 1) + ','
              + std::to_string(i * 3 + 2) + '\n';
    }

    // Compare without letting Catch2 expand the huge column name on failure.
    auto require_col_names = [](CSVReader& reader, const std::string& first) {
        const auto names = reader.get_col_names();
        REQUIRE(names.size() == 3);
        REQUIRE(names[0].size() == first.size());
        REQUIRE((names[0] == first));
        REQUIRE(names[1] == "B");
        REQUIRE(names[2] == "C");
    };

    SECTION("Header row fits in chunk_size") {
        const std::string long_name = "A" + std::string(600 * 1024, 'x');
        const std::string data = long_name + ",B,C\n" + body;

        CSVFormat format;
        format.delimiter(',').header_row(0).chunk_size(2 * 1024 * 1024).speculative_parallel_threads(1);

        auto validate_reader = [&](CSVReader& reader) {
            require_col_names(reader, long_name);

            size_t i = 0;
            for (auto& row : reader) {
                REQUIRE(row.size() == 3);
                for (size_t col = 0; col < 3; ++col) {
                    REQUIRE(row[col].get<size_t>() == i * 3 + col);
                }
                ++i;
            }
            REQUIRE(i == n_rows);
        };

        SECTION("stream path") {
            std::istringstream in(data);
            CSVReader reader(in, format);
            validate_reader(reader);
        }

        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_issue_337_long_header.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << data;
            }
            CSVReader reader(cleanup.filename, format);
            validate_reader(reader);
        }
    }

    SECTION("Header row after a short preamble row") {
        // header_row(1): the head window holds the complete preamble row but
        // not the long header row, so header trimming must resume on a later read.
        const std::string long_name = "A" + std::string(600 * 1024, 'x');
        const std::string data = "preamble\n" + long_name + ",B,C\n" + body;

        CSVFormat format;
        format.delimiter(',').header_row(1).chunk_size(2 * 1024 * 1024).speculative_parallel_threads(1);

        auto validate_reader = [&](CSVReader& reader) {
            require_col_names(reader, long_name);

            size_t i = 0;
            for (auto& row : reader) {
                REQUIRE(row.size() == 3);
                REQUIRE(row[0].get<size_t>() == i * 3);
                ++i;
            }
            REQUIRE(i == n_rows);
        };

        SECTION("stream path") {
            std::istringstream in(data);
            CSVReader reader(in, format);
            validate_reader(reader);
        }

        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_issue_337_preamble.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << data;
            }
            CSVReader reader(cleanup.filename, format);
            validate_reader(reader);
        }
    }

    SECTION("Header row starts after the first read window ends") {
        // header_row(2) with long preamble rows: the first read window ends
        // inside the header row on both paths, so trim_header() must not mark
        // the header as trimmed after popping only the preamble rows.
        // The 700KB window starting at byte 0 (stream) or after "short\n"
        // (mmap) ends inside the 150KB header row.
        const std::string preamble = "preamble," + std::string(600 * 1024, 'p') + '\n';
        const std::string long_name = "A" + std::string(150 * 1024, 'x');
        const std::string data = "short\n" + preamble + long_name + ",B,C\n" + body;

        CSVFormat format;
        format.delimiter(',')
            .header_row(2)
            .chunk_size(internals::CSV_CHUNK_SIZE_FLOOR + 200 * 1024)
            .speculative_parallel_threads(1);  // Keep the stream window at chunk_size

        auto validate_reader = [&](CSVReader& reader) {
            require_col_names(reader, long_name);

            size_t i = 0;
            for (auto& row : reader) {
                REQUIRE(row.size() == 3);
                REQUIRE(row[2].get<size_t>() == i * 3 + 2);
                ++i;
            }
            REQUIRE(i == n_rows);
        };

        SECTION("stream path") {
            std::istringstream in(data);
            CSVReader reader(in, format);
            validate_reader(reader);
        }

        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_issue_337_late_header.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << data;
            }
            CSVReader reader(cleanup.filename, format);
            validate_reader(reader);
        }
    }

    SECTION("UTF-8 BOM before a header row longer than the head buffer") {
        // The mmap head window completes no row, so MmapParser re-reads from
        // the first row's start, after the BOM. The skipped prefix must advance
        // the source offset without suppressing this fallback read.
        const std::string long_name = "A" + std::string(600 * 1024, 'x');
        const std::string data = "\xEF\xBB\xBF" + long_name + ",B,C\n" + body;

        CSVFormat format;
        format.delimiter(',').header_row(0).chunk_size(2 * 1024 * 1024).speculative_parallel_threads(1);

        auto validate_reader = [&](CSVReader& reader) {
            REQUIRE(reader.utf8_bom());
            require_col_names(reader, long_name);

            size_t i = 0;
            for (auto& row : reader) {
                REQUIRE(row.size() == 3);
                REQUIRE(row[0].get<size_t>() == i * 3);
                ++i;
            }
            REQUIRE(i == n_rows);
        };

        SECTION("stream path") {
            std::istringstream in(data);
            CSVReader reader(in, format);
            validate_reader(reader);
        }

        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_issue_337_bom_long_header.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << data;
            }
            CSVReader reader(cleanup.filename, format);
            validate_reader(reader);
        }
    }

    SECTION("Header row larger than chunk_size still throws") {
        const std::string data = "A" + std::string(1200 * 1024, 'x') + ",B,C\n" + body;

        CSVFormat format;
        format.delimiter(',').header_row(0).chunk_size(internals::CSV_CHUNK_SIZE_FLOOR).speculative_parallel_threads(1);

        auto read_all = [](CSVReader& reader) {
            for (auto& row : reader) { (void)row; }
        };

        SECTION("stream path") {
            REQUIRE_THROWS_WITH(
                [&]() {
                    std::istringstream in(data);
                    CSVReader reader(in, format);
                    read_all(reader);
                }(),
                Catch::Matchers::ContainsSubstring("chunk size")
            );
        }

        SECTION("mmap path") {
            FileGuard cleanup("./tests/data/tmp_issue_337_too_long.csv");
            {
                std::ofstream out(cleanup.filename, std::ios::binary);
                out << data;
            }
            REQUIRE_THROWS_WITH(
                [&]() {
                    CSVReader reader(cleanup.filename, format);
                    read_all(reader);
                }(),
                Catch::Matchers::ContainsSubstring("chunk size")
            );
        }
    }
}

TEST_CASE("Issue #218 - Infinite read loop detection", "[issue_218]") {

    SECTION("Detects when row exceeds chunk size and file doesn't end") {
        // A >2x default chunk row with margin spans three chunks; the second
        // chunk completes without finding a '\n', so _read_requested is already true →
        // the infinite-loop guard fires.
        std::stringstream ss;
        ss << "A,B\n" << "1,2\n" << guard_trip_row_2col();

        CSVFormat serial_format;
        serial_format.threading(false);
        CSVReader reader(ss, serial_format);

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
