#include "catch.hpp"
#include "csv_parser.hpp"
using namespace csv;

CSVRow make_row() {
    auto col_names = std::make_shared<internals::ColNames>(
        std::vector<std::string>({ "A", "B", "C", "D" })
    );

    std::string str;
    str += "Col1"
        "Col2"
        "Col3"
        "Col4";

    std::vector<size_t> splits = { 4, 8, 12 };
    return CSVRow(std::move(str), std::move(splits), col_names);
}

// Tests of the CSVRow Data Structure
TEST_CASE("CSVRow Size Check", "[test_csv_row_size]") {
    auto row = make_row();
    REQUIRE(row.size() == 4);
}

TEST_CASE("CSVRow operator[]", "[test_csv_row_index]") {
    auto row = make_row();
    REQUIRE(row[1] == "Col2");
    REQUIRE(row["B"] == "Col2");

    REQUIRE(row[2] == "Col3");
    REQUIRE(row["C"] == "Col3");
}

TEST_CASE("CSVRow Content Check", "[test_csv_row_contents]") {
    auto row = make_row();
    REQUIRE(std::vector<std::string>(row) ==
        std::vector<std::string>({ "Col1", "Col2", "Col3", "Col4" }));
}

TEST_CASE("CSVRow operator==", "[test_csv_row_equal]") {
    auto row = make_row();
    auto search_obj = std::unordered_map<std::string, std::string>({
        { "A", "Col 1" },
        { "B", "Col 2" }
    });

    REQUIRE(row == search_obj);
}