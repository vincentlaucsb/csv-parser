#include "catch.hpp"
#include "csv_parser.h"
#include <string>

using namespace csv_parser;

TEST_CASE( "Recognize Integers Properly", "[dtype_int]" ) {
    std::string int_a("1");
    
    REQUIRE( data_type(int_a) ==  2 );
}

TEST_CASE( "Recognize Strings Properly", "[dtype_str]" ) {
    std::string str_a("test");
    std::string str_b("999.999.9999");
    std::string str_c("510-123-4567");
    std::string str_d("510 123");
    std::string str_e("510 123 4567");
    
    REQUIRE( data_type(str_a) ==  1 );
    REQUIRE( data_type(str_b) ==  1 );
    REQUIRE( data_type(str_c) ==  1 );
    REQUIRE( data_type(str_d) ==  1 );
    REQUIRE( data_type(str_e) ==  1 );
}

TEST_CASE( "Recognize Null Properly", "[dtype_null]" ) {
    std::string null_str("");
    REQUIRE( data_type(null_str) ==  0 );
}

TEST_CASE( "Recognize Floats Properly", "[dtype_float]" ) {
    std::string float_a("3.14");
    std::string float_b("       -3.14            ");
    
    REQUIRE( data_type(float_a) ==  3 );
    REQUIRE( data_type(float_b) ==  3 );
}