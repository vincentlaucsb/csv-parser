/** Tests of both reading and writing */

#include <array>
#include <cstdio>

#include "catch.hpp"
#include "csv.hpp"

using namespace csv;

TEST_CASE("Simple Integer Round Trip Test", "[test_roundtrip_int]") {
    auto filename = "round_trip.csv";
    std::ofstream outfile(filename, std::ios::binary);
    auto writer = make_csv_writer(outfile);

    writer << std::vector<std::string>({ "A", "B", "C", "D", "E" });

    const size_t n_rows = 1000000;

    for (int i = 0; i <= n_rows; i++) {
        auto str = std::to_string(i);
        std::array<std::string, 5> ints = { str, str, str, str, str };
        writer << ints;
    }

    CSVReader reader(filename);

    size_t i = 0;
    for (auto& row : reader) {
        for (auto& col : row) {
            REQUIRE(col == i);
        }

        i++;
    }

    REQUIRE(reader.size() == n_rows);

    remove(filename);
}