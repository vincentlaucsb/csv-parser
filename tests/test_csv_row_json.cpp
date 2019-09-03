#include "catch.hpp"
#include "csv.hpp"
using namespace csv;

TEST_CASE("CSVRow to_json_test()", "[csv_row_to_json]") {
    auto col_names = std::make_shared<internals::ColNames>(
        std::vector<std::string>({ "A", "B" })
        );

    std::string str = "Col1"
        "Col2";

    std::vector<unsigned short> splits = { 4 };

    CSVRow row(str, splits, col_names);

    REQUIRE(row.to_json() == "{\"A\":\"Col1\",\"B\":\"Col2\"}");
}

TEST_CASE("CSVRow to_json_test() with Numbers", "[csv_numeric_row_to_json]") {
    auto col_names = std::make_shared<internals::ColNames>(
        std::vector<std::string>({ "A", "B" })
        );

    std::string str = "1234.3"
        "234";

    std::vector<unsigned short> splits = { 6 };

    CSVRow row(str, splits, col_names);

    REQUIRE(row.to_json() == "{\"A\":1234.3,\"B\":234}");
}