#include "catch.hpp"
#include "csv_parser.h"
#include "print.h"
#include "getargs.h"

using namespace csv_parser;
using std::vector;
using std::string;

// Command Line Arguments
TEST_CASE("Test Command Line Argument Parsing", "[test_getargs]") {
    int argc = 5;
    char* argv[] = {
        "progname",
        "Column1",
        "Column2",
        "\"Column",
        "3\""
    };

    vector<string> args;
    vector<string> flags;

    getargs(argc, argv, args, flags);

    REQUIRE(args[0] == "Column1");
    REQUIRE(args[1] == "Column2");
    REQUIRE(args[2] == "Column 3");
}

TEST_CASE("Malformed Input", "[test_getargs_fail]") {
    int argc = 5;
    char* argv[] = {
        "progname",
        "Column1",
        "Column2",
        "\"Column",
        "3" // No quote escape termination --> should fail
    };

    vector<string> args;
    vector<string> flags;

    int fail = getargs(argc, argv, args, flags);
    REQUIRE(fail == 1);
}

// String Formatting
TEST_CASE("Round Numeric Vector", "[test_round_vec]") {
    vector<long double> input = { 3.14159, 69.6999, 69.420 };
    vector<string> expected_output = { "3.14", "69.70", "69.42" };
    
    REQUIRE(round(input) == expected_output);
}

TEST_CASE("Calculating Column Widths", "[test_get_col_widths]") {
    vector<vector<string>> input = {
        vector<string>({
            "LOOOOOOOOOOOOOOOOOOOOOOOOOOONNNNNNNNGGG BOOOOOOOOOIII", // Length: 53
            "Short Column"
        }),
        
        vector<string>({
            "Filler Text Filler Text",
            "Random Filler Random Filler"  // Length: 27
        })
    };    
    
    vector<size_t> expected_80 = { 56, 30 };
    vector<size_t> expected_40 = { 40, 30 };
    
    REQUIRE(_get_col_widths(input, 80) == expected_80);
    REQUIRE(_get_col_widths(input, 40) == expected_40);
}