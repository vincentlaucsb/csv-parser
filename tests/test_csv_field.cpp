#include "csv.hpp"
#include <chrono>
#include <catch2/catch_all.hpp>
#ifdef CSV_HAS_CXX17
#include <optional>
#endif
#include <cmath>
#include <iostream>
#include <sstream>

using namespace csv;

#include "./shared/float_test_cases.hpp"

TEMPLATE_TEST_CASE("CSVField get<> - String Value", "[test_csv_field_get_string]",
    signed char, short int, int, long long int, double, long double) {
    CSVField field("applesauce");
    REQUIRE(field.get<>() == "applesauce");

    std::string str_out;
    REQUIRE(field.try_get(str_out));
    REQUIRE(str_out == "applesauce");

    csv::string_view sv_out;
    REQUIRE(field.try_get(sv_out));
    REQUIRE(sv_out == "applesauce");

    // Assert that improper conversions attempts are thwarted
    bool ex_caught = false;
    try {
        field.get<TestType>();
    }
    catch (std::runtime_error& err) {
        REQUIRE(err.what() == csv::internals::ERROR_NAN);
        ex_caught = true;
    }

    TestType out = {};
    REQUIRE_FALSE(field.try_get(out));

    REQUIRE(ex_caught);
}

TEST_CASE("CSVField handles default string_view as empty", "[test_csv_field_get_string]") {
    CSVField field((csv::string_view()));

    REQUIRE(field.type() == DataType::CSV_NULL);
    REQUIRE(field.get<csv::string_view>().empty());
    REQUIRE(field.get<std::string>().empty());
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

    double out = 0;
    REQUIRE_FALSE(field.try_get(out));

    REQUIRE(ex_caught);
}

#ifdef CSV_HAS_STD_EXPECTED
TEST_CASE("CSVField as<T>() returns expected", "[test_csv_field_expected]") {
    //! [CSVField Expected Conversion]
    auto number = CSVField("2019").as<std::uint32_t>();
    REQUIRE(number);
    REQUIRE(*number == 2019);

    auto not_number = CSVField("applesauce").as<std::uint32_t>();
    REQUIRE_FALSE(not_number);
    REQUIRE(not_number.error() == CSVConversionError::NotANumber);

    auto overflow = CSVField("2019").as<signed char>();
    REQUIRE_FALSE(overflow);
    REQUIRE(overflow.error() == CSVConversionError::Overflow);

    auto float_to_int = CSVField("2.718").as<int>();
    REQUIRE_FALSE(float_to_int);
    REQUIRE(float_to_int.error() == CSVConversionError::FloatToInt);

    auto negative_to_unsigned = CSVField("-1").as<std::uint32_t>();
    REQUIRE_FALSE(negative_to_unsigned);
    REQUIRE(negative_to_unsigned.error() == CSVConversionError::NegativeToUnsigned);
    REQUIRE(std::string(csv_conversion_error_message(negative_to_unsigned.error())) == csv::internals::ERROR_NEG_TO_UNSIGNED);
    //! [CSVField Expected Conversion]
}
#endif

TEST_CASE("CSVField get<>() - Integral Value", "[test_csv_field_get_int]") {
    CSVField this_year("2019");
    REQUIRE(this_year.get<>() == "2019");
    REQUIRE(this_year.get<csv::string_view>() == "2019");
    REQUIRE(this_year.get<int>() == 2019);
    REQUIRE(this_year.get<long long int>() == 2019);
    REQUIRE(this_year.get<float>() == 2019.0f);
    REQUIRE(this_year.get<double>() == 2019.0);
    REQUIRE(this_year.get<long double>() == 2019l);

    int int_out = 0;
    REQUIRE(this_year.try_get(int_out));
    REQUIRE(int_out == 2019);

    long long ll_out = 0;
    REQUIRE(this_year.try_get(ll_out));
    REQUIRE(ll_out == 2019);

    double double_out = 0;
    REQUIRE(this_year.try_get(double_out));
    REQUIRE(double_out == 2019.0);

    std::string str_out;
    REQUIRE(this_year.try_get(str_out));
    REQUIRE(str_out == "2019");

    csv::string_view sv_out;
    REQUIRE(this_year.try_get(sv_out));
    REQUIRE(sv_out == "2019");

    bool ex_caught = false;
    try {
        this_year.get<signed char>();
    }
    catch (std::runtime_error& err) {
        REQUIRE(err.what() == csv::internals::ERROR_OVERFLOW);
        ex_caught = true;
    }

    signed char sc_out = 0;
    REQUIRE_FALSE(this_year.try_get(sc_out));

    REQUIRE(ex_caught);
}

#ifdef CSV_HAS_CXX17
TEST_CASE("CSVField converts to std::optional", "[test_csv_field_optional]") {
    //! [CSVField Optional Conversion]
    std::optional<std::uint32_t> number = CSVField("2019");
    REQUIRE(number);
    REQUIRE(*number == 2019);

    std::optional<std::uint32_t> not_number = CSVField("applesauce");
    REQUIRE_FALSE(not_number);

    std::optional<std::uint32_t> negative_unsigned = CSVField("-1");
    REQUIRE_FALSE(negative_unsigned);

    std::optional<bool> truth = CSVField("true");
    REQUIRE(truth);
    REQUIRE(*truth);

    std::optional<bool> numeric_bool = CSVField("1");
    REQUIRE_FALSE(numeric_bool);
    //! [CSVField Optional Conversion]
}
#endif

TEST_CASE("CSVField get<>() - Integer Boundary Value", "[test_csv_field_get_boundary]") {
    // Note: Tests may fail if compiler defines typenames differently than
    // Microsoft/GCC/clang
    REQUIRE(CSVField("127").get<signed char>() == 127);
    REQUIRE(CSVField("32767").get<short>() == 32767);
    REQUIRE(CSVField("2147483647").get<int>() == 2147483647);

    REQUIRE(CSVField("255").get<unsigned char>() == 255);
    REQUIRE(CSVField("65535").get<unsigned short>() == 65535);
    REQUIRE(CSVField("4294967295").get<unsigned>() == 4294967295);

    signed char sc_out = 0;
    REQUIRE(CSVField("127").try_get(sc_out));
    REQUIRE(sc_out == 127);

    unsigned short us_out = 0;
    REQUIRE(CSVField("65535").try_get(us_out));
    REQUIRE(us_out == 65535);
}

// Test converting a small integer to unsigned and signed integer types
TEMPLATE_TEST_CASE("CSVField get<>() - Integral Value to Int", "[test_csv_field_convert_int]",
    unsigned char, unsigned short, unsigned int, unsigned long long,
    char, short, int, long long int) {
    CSVField savage("21");
    REQUIRE(savage.get<TestType>() == 21);

    TestType out = 0;
    REQUIRE(savage.try_get(out));
    REQUIRE(out == 21);
}

TEST_CASE("CSVField get<>() - Floating Point Value", "[test_csv_field_get_float]") {
    SECTION("Test get() with various float types") {
        //! [CSVField Floating Point Conversion]
        CSVField euler("2.718");
        REQUIRE(euler.get<>() == "2.718");
        REQUIRE(euler.get<csv::string_view>() == "2.718");
        REQUIRE(euler.get<float>() == Catch::Approx(2.718f));
        REQUIRE(euler.get<double>() == Catch::Approx(2.718));
        REQUIRE(internals::is_equal(euler.get<long double>(), 2.718L));

        float float_out = 0;
        REQUIRE(euler.try_get(float_out));
        REQUIRE(float_out == Catch::Approx(2.718f));

        double double_out = 0;
        REQUIRE(euler.try_get(double_out));
        REQUIRE(double_out == Catch::Approx(2.718));

        long double long_double_out = 0;
        REQUIRE(euler.try_get(long_double_out));
        REQUIRE(long_double_out == Catch::Approx(2.718l));

        int int_out = 0;
        REQUIRE_FALSE(euler.try_get(int_out));
        //! [CSVField Floating Point Conversion]
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

TEST_CASE("CSVField try_get<long double>()", "[test_csv_field_try_get_long_double]") {
    SECTION("Numeric value") {
        CSVField field("2.718");
        long double out = 0;

        REQUIRE(field.try_get(out));
        REQUIRE(internals::is_equal(out, 2.718L));
    }

    SECTION("Non-numeric value") {
        CSVField field("not-a-number");
        long double out = 123.0L;

        REQUIRE_FALSE(field.try_get(out));
        REQUIRE(internals::is_equal(out, 123.0L));
    }
}

TEST_CASE("CSVField bool conversion requires boolean classification", "[test_csv_field_bool]") {
    //! [CSVField Bool Conversion]
    SECTION("Numeric fields are not implicitly booleans") {
        bool out = false;
        REQUIRE_FALSE(CSVField("1").try_get(out));
    }

    SECTION("Boolean literals parse as booleans") {
        bool out = false;
        REQUIRE(CSVField("true").try_get(out));
        REQUIRE(out);

        out = true;
        REQUIRE(CSVField("false").try_get(out));
        REQUIRE_FALSE(out);
    }

    SECTION("Other string fields are not implicitly booleans") {
        bool out = false;
        REQUIRE_FALSE(CSVField("truthy").try_get(out));
    }
    //! [CSVField Bool Conversion]
}

TEST_CASE("CSVField timestamp parsing", "[test_csv_field_timestamp]") {
    //! [CSVField Timestamp Conversion]
    CSVField field("1970-01-02T00:00:00.123Z");

    REQUIRE(field.type() == DataType::CSV_TIMESTAMP);

    std::uint64_t milliseconds = 0;
    REQUIRE(field.try_parse_timestamp(milliseconds));
    REQUIRE(milliseconds == 86400123);

    unsigned long long milliseconds_ull = 0;
    REQUIRE(field.try_parse_timestamp(milliseconds_ull));
    REQUIRE(milliseconds_ull == 86400123ULL);

    std::chrono::milliseconds duration_ms(0);
    REQUIRE(field.try_get(duration_ms));
    REQUIRE(duration_ms == std::chrono::milliseconds(86400123));

    std::chrono::seconds duration_s(0);
    REQUIRE(field.try_get(duration_s));
    REQUIRE(duration_s == std::chrono::seconds(86400));

    std::chrono::system_clock::time_point time_point;
    REQUIRE(field.try_get(time_point));
    REQUIRE(time_point.time_since_epoch() == std::chrono::milliseconds(86400123));

    CSVField integer_timestamp("86400123");
    std::chrono::seconds coerced_seconds(0);
    REQUIRE(integer_timestamp.try_parse_timestamp(coerced_seconds));
    REQUIRE(coerced_seconds == std::chrono::seconds(86400));

    std::uint64_t unchanged = 123;
    REQUIRE_FALSE(CSVField("not-a-timestamp").try_parse_timestamp(unchanged));
    REQUIRE(unchanged == 123);

    unchanged = 123;
    REQUIRE_FALSE(CSVField("-1").try_parse_timestamp(unchanged));
    REQUIRE(unchanged == 123);
    //! [CSVField Timestamp Conversion]
}

TEST_CASE("CSVField try_parse_hex()", "[test_csv_field_parse_hex]") {
    //! [CSVField Hex Conversion]
    long long value = 0;

    SECTION("Valid Hex Values") {
        std::unordered_map<std::string, long long> test_cases = {
            {"  A   ", 10},
            {"0A", 10},
            {"0B", 11},
            {"0C", 12},
            {"0D", 13},
            {"0E", 14},
            {"0F", 15},
            {"0x10", 16},
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

    SECTION("Reject Values Outside Target Type Range") {
        unsigned char byte_value = 0;
        REQUIRE(CSVField("FF").try_parse_hex(byte_value));
        REQUIRE(byte_value == 255);
        REQUIRE_FALSE(CSVField("100").try_parse_hex(byte_value));

        signed char signed_byte_value = 0;
        REQUIRE(CSVField("7F").try_parse_hex(signed_byte_value));
        REQUIRE(signed_byte_value == 127);
        REQUIRE_FALSE(CSVField("80").try_parse_hex(signed_byte_value));

        unsigned int unsigned_value = 0;
        REQUIRE_FALSE(CSVField("-1").try_parse_hex(unsigned_value));
    }
    //! [CSVField Hex Conversion]
}


TEST_CASE("CSVField try_parse_decimal()", "[test_csv_field_parse_hex]") {
    //! [CSVField Decimal Separator Conversion]
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
    //! [CSVField Decimal Separator Conversion]
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

    TestType out = 0;
    REQUIRE_FALSE(euler.try_get(out));

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

    TestType out = 0;
    REQUIRE_FALSE(neg.try_get(out));

    REQUIRE(ex_caught);
}

TEST_CASE("CSVField Equality Operator", "[test_csv_field_operator==]") {
    CSVField field("3.14");
    REQUIRE(field == "3.14");
    REQUIRE(field == 3.14f);
    REQUIRE(field == 3.14);
}
