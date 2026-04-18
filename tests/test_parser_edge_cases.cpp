#include <catch2/catch_all.hpp>

#include <fstream>
#include <sstream>
#include <vector>

#include "csv.hpp"
#include "shared/file_guard.hpp"

using namespace csv;

#ifndef __EMSCRIPTEN__
namespace {
    std::string make_crlf_split_csv(size_t chunk_size) {
        // Arrange bytes so row1 ends with '\r' at chunk boundary and '\n' in next chunk:
        // [ ... <chunk_size - 1> = '\r' ][ <chunk_size> = '\n' ]
        const size_t first_field_len = chunk_size - 3;

        std::string csv_text;
        csv_text.reserve(chunk_size + 64);
        csv_text.append(first_field_len, 'X');
        csv_text += ",B\r\n";
        csv_text += "C,D\r\n";
        return csv_text;
    }

    void validate_no_synthetic_row(CSVReader& reader, size_t expected_first_field_len) {
        std::vector<std::vector<std::string>> rows;
        REQUIRE_NOTHROW([
            &rows,
            &reader
        ]() {
            for (auto& row : reader) {
                rows.emplace_back(std::vector<std::string>(row));
            }
        }());

        // The parser should never synthesize an extra row in this split-CRLF case.
        REQUIRE(rows.size() >= 1);
        REQUIRE(rows.size() <= 2);
        REQUIRE(rows[0].size() == 2);

        REQUIRE(rows[0][0].size() == expected_first_field_len);
        REQUIRE(rows[0][1] == "B");

        if (rows.size() == 2) {
            REQUIRE(rows[1].size() == 2);
            REQUIRE(rows[1][0] == "C");
            REQUIRE(rows[1][1] == "D");
        }
    }
}

TEST_CASE("PR #271 - CRLF split does not emit synthetic row", "[pr_271][parser_edge_cases]") {
    const size_t chunk_size = internals::CSV_CHUNK_SIZE_FLOOR;
    const size_t expected_first_field_len = chunk_size - 3;

    CSVFormat format;
    format
        .no_header()
        .variable_columns(VariableColumnPolicy::KEEP)
        .chunk_size(chunk_size);

    SECTION("stream path") {
        std::stringstream ss(make_crlf_split_csv(chunk_size));
        CSVReader reader(ss, format);
        validate_no_synthetic_row(reader, expected_first_field_len);
    }

    SECTION("mmap path") {
        FileGuard cleanup("./tests/data/tmp_issue_271_crlf_split.csv");
        {
            std::ofstream out(cleanup.filename, std::ios::binary);
            out << make_crlf_split_csv(chunk_size);
        }

        CSVReader reader(cleanup.filename, format);
        validate_no_synthetic_row(reader, expected_first_field_len);
    }
}
#endif
