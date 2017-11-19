#include "catch.hpp"
#include "csv_parser.h"
#include "print.h"
#include "getargs.h"

using namespace csv_parser;
using std::vector;
using std::string;

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