#include "catch.hpp"
#include "csv_parser.hpp"
using namespace csv;

CSVRow make_row() {
    std::string str;
    str += "Col1"
        "Col2"
        "Col3"
        "Col4";

    std::vector<size_t> splits = { 4, 8, 12 };

    return CSVRow(std::move(str), std::move(splits));
}

// Tests of the CSVRow Data Structure
TEST_CASE("CSVRow Size Check", "[test_csv_row_size]") {
    auto row = make_row();
    REQUIRE(row.size() == 4);
}

TEST_CASE("CSVRow operator[]", "[test_csv_row_index]") {
    auto row = make_row();
    REQUIRE(row[1] == "Col2");
    REQUIRE(row[2] == "Col3");
}

TEST_CASE("CSVRow Content Check", "[test_csv_row_contents]") {
    auto row = make_row();
    REQUIRE(std::vector<std::string>(row) ==
        std::vector<std::string>({ "Col1", "Col2", "Col3", "Col4" }));
}