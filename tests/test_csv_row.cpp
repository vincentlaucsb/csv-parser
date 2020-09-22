// Tests for the CSVRow and CSVField Data Structures

#include "catch.hpp"
#include "csv.hpp"
using namespace csv;

// Construct a CSVRow and assert that its interface works as expected
TEST_CASE("CSVRow Test", "[test_csv_row]") {
    auto reader = "A,B,C,D\r\n"
                  "Col1,Col2,Col3,Col4"_csv;

    CSVRow row;
    reader.read_row(row);
    
    bool error_caught = false;

    SECTION("size() Check") {
        REQUIRE(row.size() == 4);
    }

    SECTION("operator[]") {
        REQUIRE(row[1] == "Col2");
        REQUIRE(row["B"] == "Col2");

        REQUIRE(row[2] == "Col3");
        REQUIRE(row["C"] == "Col3");
    }

    SECTION("operator[] Out of Bounds") {
        try {
            row[4].get<>();
        }
        catch (std::runtime_error&) {
            error_caught = true;
        }

        REQUIRE(error_caught);
    }

    SECTION("operator[] Access Non-Existent Column") {
        try {
            row["Col5"].get<>();
        }
        catch (std::runtime_error&) {
            error_caught = true;
        }

        REQUIRE(error_caught);
    }

    SECTION("Content Check") {
        REQUIRE(std::vector<std::string>(row) ==
            std::vector<std::string>({ "Col1", "Col2", "Col3", "Col4" }));
    }

    /** Allow get_sv() to be used with a const CSVField
     *  
     *  See: https://github.com/vincentlaucsb/csv-parser/issues/86
     *
     */
    SECTION("get_sv() Check") {
        std::vector<std::string> content;

        for (const auto& field : row) {
            content.push_back(std::string(field.get_sv()));
        }

        REQUIRE(std::vector<std::string>(row) ==
            std::vector<std::string>({ "Col1", "Col2", "Col3", "Col4" }));
    }
}

// Integration test for CSVRow/CSVField
TEST_CASE("CSVField operator==", "[test_csv_field_equal]") {
    auto reader = "A,B,C,D\r\n"
                  "1,2,3,3.14"_csv;

    CSVRow row;
    reader.read_row(row);

    REQUIRE(row["A"] == 1);
    REQUIRE(row["B"] == 2);
    REQUIRE(row["C"] == 3);
    REQUIRE(internals::is_equal(row["D"].get<long double>(), 3.14L));
}