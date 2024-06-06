#include "csv.hpp"
#include <catch2/catch_all.hpp>
#include <cmath>
#include <iostream>

using namespace csv;

#include "./shared/float_test_cases.hpp"

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

TEST_CASE("CSVField get<>() - Integer Boundary Value", "[test_csv_field_get_boundary]") {
    // Note: Tests may fail if compiler defines typenames differently than
    // Microsoft/GCC/clang
    REQUIRE(CSVField("127").get<signed char>() == 127);
    REQUIRE(CSVField("32767").get<short>() == 32767);
    REQUIRE(CSVField("2147483647").get<int>() == 2147483647);

    REQUIRE(CSVField("255").get<unsigned char>() == 255);
    REQUIRE(CSVField("65535").get<unsigned short>() == 65535);
    REQUIRE(CSVField("4294967295").get<unsigned>() == 4294967295);
}

// Test converting a small integer to unsigned and signed integer types
TEMPLATE_TEST_CASE("CSVField get<>() - Integral Value to Int", "[test_csv_field_convert_int]",
    unsigned char, unsigned short, unsigned int, unsigned long long,
    char, short, int, long long int) {
    CSVField savage("21");
    REQUIRE(savage.get<TestType>() == 21);
}

TEST_CASE("CSVField get<>() - Floating Point Value", "[test_csv_field_get_float]") {
    SECTION("Test get() with various float types") {
        CSVField euler("2.718");
        REQUIRE(euler.get<>() == "2.718");
        REQUIRE(euler.get<csv::string_view>() == "2.718");
        REQUIRE(euler.get<float>() == 2.718f);
        REQUIRE(euler.get<double>() == 2.718);
        REQUIRE(euler.get<long double>() == 2.718l);
    }

    SECTION("Test get() with various values") {
        std::string input;
        long double expected = 0;

        std::tie(input, expected) =
            GENERATE(table<std::string, long double>(
                csv_test::FLOAT_TEST_CASES));

        CSVField testField(input);

        REQUIRE(internals::is_equal(testField.get<long double>(), expected));
    }
}

TEST_CASE("CSVField try_parse_hex()", "[test_csv_field_parse_hex]") {
    int value = 0;

    SECTION("Valid Hex Values") {
        std::unordered_map<std::string, int> test_cases = {
            {"  A   ", 10},
            {"0A", 10},
            {"0B", 11},
            {"0C", 12},
            {"0D", 13},
            {"0E", 14},
            {"0F", 15},
            {"FF", 255},
            {"B00B5", 721077},
            {"D3ADB33F", 3551376191},
            {"  D3ADB33F  ", 3551376191}
        };

        for (auto& _case : test_cases) {
            REQUIRE(CSVField(_case.first).try_parse_hex(value));
            REQUIRE(value == _case.second);
        }
    }

    SECTION("Invalid Values") {
        std::vector<std::string> invalid_test_cases = {
            "", "    ", "carneasda", "carne asada", "0fg"
        };

        for (auto& _case : invalid_test_cases) {
            REQUIRE(CSVField(_case).try_parse_hex(value) == false);
        }
    }
}


TEST_CASE("CSVField try_parse_decimal()", "[test_csv_field_parse_hex]") {
    SECTION("Test try_parse_decimal() with non-numeric value") {
        long double output = 0;
        std::string input = "stroustrup";
        CSVField testField(input);

        REQUIRE(testField.try_parse_decimal(output, ',') == false);
        REQUIRE(testField.type() == DataType::CSV_STRING);
    }

    SECTION("Test try_parse_decimal() with integer value") {
        long double output = 0;
        std::string input = "2024";
        CSVField testField(input);

        REQUIRE(testField.try_parse_decimal(output, ',') == true);
        REQUIRE(testField.type() == DataType::CSV_INT16);
        REQUIRE(internals::is_equal(output, 2024.0l));
    }

    SECTION("Test try_parse_decimal() with various valid values") {
        std::string input;
        long double output = 0;
        long double expected = 0;

        std::tie(input, expected) =
            GENERATE(table<std::string, long double>(
                csv_test::FLOAT_TEST_CASES));

        // Replace '.' with ','
        std::replace(input.begin(), input.end(), '.', ',');

        CSVField testField(input);

        REQUIRE(testField.try_parse_decimal(output, ',') == true);
        REQUIRE(testField.type() == DataType::CSV_DOUBLE);
        REQUIRE(internals::is_equal(output, expected));
    }
}

TEMPLATE_TEST_CASE("CSVField get<>() - Disallow Float to Int", "[test_csv_field_get_float_as_int]",
    unsigned char, unsigned short, unsigned int, unsigned long long int,
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

TEMPLATE_TEST_CASE("CSVField get<>() - Disallow Negative to Unsigned", "[test_csv_field_no_unsigned_neg]",
    unsigned char, unsigned short, unsigned int, unsigned long long int) {
    CSVField neg("-1337");
    bool ex_caught = false;

    try {
        neg.get<TestType>();
    }
    catch (std::runtime_error& err) {
        REQUIRE(err.what() == csv::internals::ERROR_NEG_TO_UNSIGNED);
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