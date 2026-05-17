#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/file_guard.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace csv;

namespace {
    void write_file(const std::string& filename, const std::string& content) {
        std::ofstream out(filename, std::ios::binary);
        out << content;
    }

    std::vector<size_t> row_offsets(CSVReader& reader) {
        std::vector<size_t> offsets;
        for (auto& row : reader) {
            offsets.push_back(row.byte_offset());
        }
        return offsets;
    }
}

TEST_CASE("CSVRow byte_offset reports source offsets", "[csv_row][offsets]") {
#if !defined(__EMSCRIPTEN__)
    SECTION("mmap LF rows") {
        FileGuard cleanup("./tests/data/tmp_row_offsets_lf.csv");
        write_file(cleanup.filename, "a,b\n1,2\n3,4\n");

        CSVFormat format;
        format.header_row(0);
        CSVReader reader(cleanup.filename, format);

        REQUIRE(row_offsets(reader) == std::vector<size_t>{ 4, 8 });
    }

    SECTION("mmap CRLF rows") {
        FileGuard cleanup("./tests/data/tmp_row_offsets_crlf.csv");
        write_file(cleanup.filename, "a,b\r\n1,2\r\n3,4\r\n");

        CSVFormat format;
        format.header_row(0);
        CSVReader reader(cleanup.filename, format);

        REQUIRE(row_offsets(reader) == std::vector<size_t>{ 5, 10 });
    }

    SECTION("mmap quoted multiline rows") {
        FileGuard cleanup("./tests/data/tmp_row_offsets_multiline.csv");
        write_file(cleanup.filename, "a,b\n\"x\ny\",2\nz,3\n");

        CSVFormat format;
        format.header_row(0);
        CSVReader reader(cleanup.filename, format);

        REQUIRE(row_offsets(reader) == std::vector<size_t>{ 4, 12 });
    }
#endif

    SECTION("stream rows") {
        std::stringstream input("a,b\n1,2\n3,4\n");
        CSVFormat format;
        format.header_row(0);
        CSVReader reader(input, format);

        REQUIRE(row_offsets(reader) == std::vector<size_t>{ 4, 8 });
    }

    SECTION("default row") {
        CSVRow row;
        REQUIRE(row.byte_offset() == 0);
    }
}

#if !defined(__EMSCRIPTEN__) && CSV_ENABLE_THREADS
TEST_CASE("CSVRow byte_offset survives speculative chunk repair", "[csv_row][offsets][speculative]") {
    FileGuard cleanup("./tests/data/tmp_row_offsets_speculative.csv");

    std::string content;
    content.reserve(internals::CSV_CHUNK_SIZE_FLOOR * 2 + 1024);
    content += "first,ok\n";
    while (content.size() < internals::CSV_CHUNK_SIZE_FLOOR - 32) {
        content += "filler,row\n";
    }

    const size_t split_offset = content.size();
    content += "split,\"";
    content.append(internals::CSV_CHUNK_SIZE_FLOOR, 'x');
    content += "\",done\n";
    const size_t after_offset = content.size();
    content += "after,row\n";
    write_file(cleanup.filename, content);

    CSVFormat format;
    format.no_header()
        .delimiter(',')
        .chunk_size(internals::CSV_CHUNK_SIZE_FLOOR)
        .speculative_parallel_min_bytes(1)
        .speculative_parallel_threads(2);

    CSVReader reader(cleanup.filename, format);
    REQUIRE(reader.parse_worker_count() == 2);

    size_t observed_split_offset = 0;
    size_t observed_after_offset = 0;
    for (auto& row : reader) {
        const std::string first_field = row[0].get<std::string>();
        if (first_field == "split") {
            observed_split_offset = row.byte_offset();
        }
        else if (first_field == "after") {
            observed_after_offset = row.byte_offset();
        }
    }

    REQUIRE(observed_split_offset == split_offset);
    REQUIRE(observed_after_offset == after_offset);
}
#endif
