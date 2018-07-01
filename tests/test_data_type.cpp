#include "catch.hpp"
#include "csv_parser.hpp"
#include <string>

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
    REQUIRE(is_equal(out, 3.14));

    REQUIRE(data_type(float_b, &out) ==  CSV_DOUBLE);
    REQUIRE(is_equal(out, -3.14));

    REQUIRE(data_type(e, &out) == CSV_DOUBLE);
    REQUIRE(is_equal(out, 2.71828));
}