# define CATCH_CONFIG_MAIN
# include "catch.hpp"
# include "data_type.cpp"
# include <string>

using namespace csv_parser;

TEST_CASE( "Recognize Integers Properly", "[dtype_int]" ) {
    std::string int_a("1");
    
    REQUIRE( data_type(int_a) ==  2 );
}

TEST_CASE( "Recognize Strings Properly", "[dtype_str]" ) {
    std::string str_a("test");
    std::string str_b("999.999.9999");
    
    REQUIRE( data_type(str_a) ==  1 );
    REQUIRE( data_type(str_b) ==  1 );
}

TEST_CASE( "Recognize Floats Properly", "[dtype_float]" ) {
    std::string float_a("3.14");
    std::string float_b("       -3.14            ");
    
    REQUIRE( data_type(float_a) ==  3 );
    REQUIRE( data_type(float_b) ==  3 );
}