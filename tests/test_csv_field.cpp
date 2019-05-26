#include "catch.hpp"
#include "csv.hpp"
using namespace csv;

TEMPLATE_TEST_CASE("CSVField get<> - String Value", "[test_csv_field_get_string]",
    int, long long int, double, long double) {
    CSVField field("applesauce");
    REQUIRE(field.get<>() == "applesauce");

    // Assert that improper conversions attempts are thwarted
    bool ex_caught = false;
    try {
        field.get<TestType>();
    }
    catch (std::runtime_error& err) {
        ex_caught = true;
    }

    REQUIRE(ex_caught);
}

TEST_CASE("CSVField get<> - Error Messages", "[test_csv_field_get_error]") {
    CSVField field("applesauce");
    
    bool ex_caught = false;
    try {
        field.get<double>();
    }
    catch (std::runtime_error& err) {
        REQUIRE(err.what() == std::string("Attempted to convert a "
            "value of type string to double."));
        ex_caught = true;
    }

    REQUIRE(ex_caught);
}

TEST_CASE("CSVField get<>() - Floating Point Value", "[test_csv_field_get_float]") {
    CSVField euler("2.718");
    REQUIRE(euler.get<>() == "2.718");
    REQUIRE(euler.get<csv::string_view>() == "2.718");
    // REQUIRE(euler.get<float>() == 2.718f);
    REQUIRE(euler.get<double>() == 2.718);
    REQUIRE(euler.get<long double>() == 2.718l);
}

TEST_CASE("CSVField Equality Operator", "[test_csv_field_operator==]") {
    CSVField field("3.14");
    REQUIRE(field == "3.14");
    REQUIRE(field == 3.14);
}