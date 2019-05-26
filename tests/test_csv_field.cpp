#include "csv.hpp"
#include "catch.hpp"
#include <cmath>
#include <iostream>
using namespace csv;

TEMPLATE_TEST_CASE("CSVField get<> - String Value", "[test_csv_field_get_string]",
    signed char, short int, int, long long int, double, long double) {
    CSVField field("applesauce");
    REQUIRE(field.get<>() == "applesauce");

    // Assert that improper conversions attempts are thwarted
    bool ex_caught = false;
    try {
        field.get<TestType>();
    }
    catch (std::runtime_error& err) {
        REQUIRE(err.what() == csv::internals::ERROR_NAN);
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
        REQUIRE(err.what() == csv::internals::ERROR_NAN);
        ex_caught = true;
    }

    REQUIRE(ex_caught);
}

TEST_CASE("CSVField get<>() - Integral Value", "[test_csv_field_get_int]") {
    CSVField this_year("2019");
    REQUIRE(this_year.get<>() == "2019");
    REQUIRE(this_year.get<csv::string_view>() == "2019");
    REQUIRE(this_year.get<int>() == 2019);
    REQUIRE(this_year.get<long long int>() == 2019);
    REQUIRE(this_year.get<float>() == 2019.0f);
    REQUIRE(this_year.get<double>() == 2019.0);
    REQUIRE(this_year.get<long double>() == 2019l);

    bool ex_caught = false;
    try {
        this_year.get<signed char>();
    }
    catch (std::runtime_error& err) {
        REQUIRE(err.what() == csv::internals::ERROR_OVERFLOW);
        ex_caught = true;
    }

    REQUIRE(ex_caught);
}

TEST_CASE("CSVField get<>() - Floating Point Value", "[test_csv_field_get_float]") {
    CSVField euler("2.718");
    REQUIRE(euler.get<>() == "2.718");
    REQUIRE(euler.get<csv::string_view>() == "2.718");
    REQUIRE(euler.get<float>() == 2.718f);
    REQUIRE(euler.get<double>() == 2.718);
    REQUIRE(euler.get<long double>() == 2.718l);
}

TEMPLATE_TEST_CASE("CSVField get<>() - Disallow Float to Int", "[test_csv_field_get_float_as_int]",
    signed char, short, int, long long int) {
    CSVField euler("2.718");
    bool ex_caught = false;

    try {
        euler.get<TestType>();
    }
    catch (std::runtime_error& err) {
        REQUIRE(err.what() == csv::internals::ERROR_FLOAT_TO_INT);
        ex_caught = true;
    }

    REQUIRE(ex_caught);
}

TEST_CASE("CSVField Equality Operator", "[test_csv_field_operator==]") {
    CSVField field("3.14");
    REQUIRE(field == "3.14");
    REQUIRE(field == 3.14f);
    REQUIRE(field == 3.14);
}