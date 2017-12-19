#include "catch.hpp"
#include "csv_parser.h"
#include "print.h"
#include "getargs.h"
#include <regex>

using namespace csv_parser;
using namespace csv_parser::helpers;
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

    deque<string> args;
    deque<string> flags;

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

    deque<string> args;
    deque<string> flags;

    int fail = getargs(argc, argv, args, flags);
    REQUIRE(fail == 1);
}

// String Formatting
TEST_CASE("Round Numeric Vector", "[test_round_vec]") {
    vector<long double> input1 = { 3.14159, 69.6999, 69.420 };
    vector<long double> input2 = { 3.14159, NAN, 69.420 };
    vector<string> expected1 = { "3.14", "69.70", "69.42" };
    vector<string> expected2 = { "3.14", "", "69.42" };
    
    REQUIRE(round(input1) == expected1);
    REQUIRE(round(input2) == expected2);
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

TEST_CASE("Print Table", "[test_print_table]") {
    vector<vector<string>> print_rows = {
        { "A", "B", "C", "D" },
        { "1", "2", "3", "4" },
        { "1", "2", "3", "4" },
        { "1", "2", "3", "4" },
        { "1", "2", "3", "4" },
        { "1", "2", "3", "4" }
    };

    std::stringstream output;
    std::streambuf * prev_out = std::cout.rdbuf(output.rdbuf());
    helpers::print_table(print_rows, 0, {}, true);
    std::string output_str = output.str();

    // Reset std::cout
    std::cout.rdbuf(prev_out);

    // Make sure that header rows were printed out
    std::regex header_match("A\\s+B\\s+C\\s+D");
    std::smatch matches;
    std::regex_search(output_str, matches, header_match);
    REQUIRE(!matches.empty());

    // Make sure that row numbers were printed out
    REQUIRE(output_str.find("[1]") != std::string::npos);
    REQUIRE(output_str.find("[2]") != std::string::npos);
    REQUIRE(output_str.find("[3]") != std::string::npos);
    REQUIRE(output_str.find("[4]") != std::string::npos);
}