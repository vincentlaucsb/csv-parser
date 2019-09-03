#include "catch.hpp"
#include "csv.hpp"
using namespace csv;

/** Construct a CSVRow object for testing given column names and CSV fields */
CSVRow make_csv_row(std::vector<std::string> data, std::vector<std::string> col_names) {
    // Concatenate vector or strings into one large string
    std::string concat;
    std::vector<unsigned short> splits = {};

    for (auto& field : data) {
        concat += field;
        splits.push_back(concat.size());
    }

    return CSVRow(concat, splits, std::make_shared<internals::ColNames>(col_names));
}

TEST_CASE("CSVRow to_json() Test", "[csv_row_to_json]") {
    CSVRow row = make_csv_row(
        { "Col 1", "Col 2" },   // Fields
        { "A", "B" }            // Column names
    );

    REQUIRE(row.to_json() == "{\"A\":\"Col 1\",\"B\":\"Col 2\"}");
}

TEST_CASE("CSVRow to_json() Test with Numbers", "[csv_numeric_row_to_json]") {
    CSVRow row = make_csv_row(
        { "1234.3", "234" },    // Fields
        { "A", "B"}             // Column names
    );

    REQUIRE(row.to_json() == "{\"A\":1234.3,\"B\":234}");
}

TEST_CASE("CSVRow to_json() Test - Mixed", "[csv_mixed_row_to_json]") {
    CSVRow row = make_csv_row(
        { "1234.3", "234", "ABCD", "AB1", "1337" },     // Fields
        { "A", "B", "C", "D", "E" }                     // Column names
    );

    REQUIRE(row.to_json() == "{\"A\":1234.3,\"B\":234,\"C\":\"ABCD\",\"D\":\"AB1\",\"E\":1337}");
}

TEST_CASE("CSVRow to_json_array() Test() - Mixed", "[csv_mixed_row_to_json_array]") {
    CSVRow row = make_csv_row(
        { "1234.3", "234", "ABCD", "AB1", "1337" },     // Fields
        { "A", "B", "C", "D", "E" }                     // Column names
    );

    REQUIRE(row.to_json_array() == "[1234.3,234,\"ABCD\",\"AB1\",1337]");
}