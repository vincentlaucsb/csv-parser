/** Tests of both reading and writing */

#include <array>
#include <cstdio>
#include <iostream>

#include "catch.hpp"
#include "csv.hpp"

using namespace csv;

TEST_CASE("Simple Buffered Integer Round Trip Test", "[test_roundtrip_int]") {
    auto filename = "round_trip.csv";
    std::ofstream outfile(filename, std::ios::binary);
    auto writer = make_csv_writer_buffered(outfile);

    writer << std::vector<std::string>({"A", "B", "C", "D", "E"});

    const size_t n_rows = 1000000;

    for (size_t i = 0; i <= n_rows; i++) {
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

    remove(filename);
}

TEST_CASE("Simple Integer Round Trip Test", "[test_roundtrip_int]") {
    auto filename = "round_trip.csv";
    std::ofstream outfile(filename, std::ios::binary);
    auto writer = make_csv_writer(outfile);

    writer << std::vector<std::string>({ "A", "B", "C", "D", "E" });

    const size_t n_rows = 1000000;

    for (size_t i = 0; i <= n_rows; i++) {
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

    remove(filename);
}