#include "catch.hpp"
#include "csv.hpp"
#include <string>

#define INT_IS_LONG_INT

using namespace csv;
using namespace csv::internals;

TEST_CASE( "Recognize Integers Properly", "[dtype_int]" ) {
    std::string a("1"), b(" 2018   "), c(" -69 ");
    long double out;

    REQUIRE(data_type(a, &out) ==  CSV_INT);
    REQUIRE(out == 1);

    REQUIRE(data_type(b, &out) == CSV_INT);
    REQUIRE(out == 2018);

    REQUIRE(data_type(c, &out) == CSV_INT);
    REQUIRE(out == -69);
}

TEST_CASE( "Recognize Strings Properly", "[dtype_str]" ) {
    std::string str_a("test");
    std::string str_b("999.999.9999");
    std::string str_c("510-123-4567");
    std::string str_d("510 123");
    std::string str_e("510 123 4567");
    
    REQUIRE( data_type(str_a) ==  CSV_STRING );
    REQUIRE( data_type(str_b) ==  CSV_STRING );
    REQUIRE( data_type(str_c) ==  CSV_STRING );
    REQUIRE( data_type(str_d) ==  CSV_STRING );
    REQUIRE( data_type(str_e) ==  CSV_STRING );
}

TEST_CASE( "Recognize Null Properly", "[dtype_null]" ) {
    std::string null_str("");
    REQUIRE( data_type(null_str) ==  CSV_NULL );
}

TEST_CASE( "Recognize Floats Properly", "[dtype_float]" ) {
    std::string float_a("3.14"),
        float_b("       -3.14            "),
        e("2.71828");

    long double out;
    
    REQUIRE(data_type(float_a, &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 3.14L));

    REQUIRE(data_type(float_b, &out) ==  CSV_DOUBLE);
    REQUIRE(is_equal(out, -3.14L));

    REQUIRE(data_type(e, &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 2.71828L));
}

TEST_CASE("Integer Overflow", "[int_overflow]") {
    constexpr long double _INT_MAX = (long double)std::numeric_limits<int>::max();
    constexpr long double _LONG_MAX = (long double)std::numeric_limits<long int>::max();
    constexpr long double _LONG_LONG_MAX = (long double)std::numeric_limits<long long int>::max();

    std::string s;
    long double out;
    
    s = std::to_string((long long)_INT_MAX + 1);

    #if __cplusplus == 201703L
    if constexpr (_INT_MAX == _LONG_MAX) {
    #else
    if (_INT_MAX == _LONG_MAX) {
    #endif    
        REQUIRE(data_type(s, &out) == CSV_LONG_LONG_INT);
    }
    else {
        REQUIRE(data_type(s, &out) == CSV_LONG_INT);
    }

    REQUIRE(out == (long long)_INT_MAX + 1);
}

TEST_CASE( "Recognize Sub-Unit Double Values", "[regression_double]" ) {
    std::string s("0.15");
    long double out;
    REQUIRE(data_type(s, &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 0.15L));
}

TEST_CASE( "Recognize Double Values", "[regression_double2]" ) {
    // Test converting double values back and forth
    long double out;
    std::string s;

    for (long double i = 0; i <= 2.0; i += 0.01) {
        s = std::to_string(i);
        REQUIRE(data_type(s, &out) == CSV_DOUBLE);
        REQUIRE(is_equal(out, i));
    }
}

TEST_CASE("Parse Scientific Notation", "[e_notation]") {
    // Test parsing e notation
    long double out;

    std::vector<std::string> _455_thousand = {
        "4.55e5", "4.55E5",
        "4.55E+5", "4.55e+5",
        "4.55E+05",
        "4.55e0000005", "4.55E0000005",
        "4.55e+0000005", "4.55E+0000005"
    };

    for (auto number : _455_thousand) {
        REQUIRE(data_type(number, &out) == CSV_DOUBLE);
        REQUIRE(is_equal(out, 455000.0L));
    }

    REQUIRE(data_type("2.17222E+02", &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 217.222L));

    REQUIRE(data_type("4.55E+10", &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 45500000000.0L));

    REQUIRE(data_type("4.55E+11", &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 455000000000.0L));

    REQUIRE(data_type("4.55E-1", &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 0.455L));

    REQUIRE(data_type("4.55E-5", &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 0.0000455L));

    REQUIRE(data_type("4.55E-000000000005", &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 0.0000455L));
}

TEST_CASE("Parse Scientific Notation Malformed", "[sci_notation]") {
    // Assert parsing butchered scientific notation won't cause a 
    // crash or any other weird side effects
    long double out;

    std::vector<std::string> butchered = {
        "4.55E000a",
        "4.55000x40",
        "4.55000E40E40",
    };

    for (auto& s : butchered) {
        REQUIRE(data_type(s, &out) == CSV_STRING);
    }
}