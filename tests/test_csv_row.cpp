// Tests for the CSVRow and CSVField Data Structures

#include "catch.hpp"
#include "csv_parser.hpp"
using namespace csv;

CSVRow make_row();
CSVRow make_numeric_row();

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

CSVRow make_numeric_row() {
    auto col_names = std::make_shared<internals::ColNames>(
        std::vector<std::string>({ "A", "B", "C", "D" })
        );

    std::string str;
    str += "1"
        "2"
        "3"
        "3.14";

    std::vector<size_t> splits = { 1, 2, 3 };
    return CSVRow(std::move(str), std::move(splits), col_names);
}

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

TEST_CASE("CSVField operator==", "[test_csv_field_equal]") {
    auto row = make_numeric_row();
    REQUIRE(row["A"] == 1);
    REQUIRE(row["B"] == 2);
    REQUIRE(row["C"] == 3);
    REQUIRE(row["D"] == 3.14);
}