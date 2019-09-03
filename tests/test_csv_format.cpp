#include "catch.hpp"
#include "csv.hpp"
using namespace csv;

static std::string err_preamble = "There should be no overlap between "
    "the quote character, the set of possible "
    "delimiters and the set of whitespace characters.";

// Assert that an error is thrown if whitespace, delimiter, and quote 
TEST_CASE("CSVFormat - Overlapping Characters", "[csv_format_overlap]") {
    CSVFormat format;
    bool err_caught = false;

    SECTION("Tab") {
        try {
            format.delimiter('\t').quote('"').trim({ '\t' });
        }
        catch (std::runtime_error& err) {
            err_caught = true;
            REQUIRE(err.what() == std::string(err_preamble + " Offending characters: '\t'."));
        }

        REQUIRE(err_caught);
    }

    SECTION("Tab with multiple other characters") {
        try {
            format.delimiter({ ',', '\t' }).quote('"').trim({ ' ', '\t' });
        }
        catch (std::runtime_error& err) {
            err_caught = true;
            REQUIRE(err.what() == std::string(err_preamble + " Offending characters: '\t'."));
        }

        REQUIRE(err_caught);
    }

    SECTION("Repeated quote") {
        try {
            format.delimiter({ ',', '"' }).quote('"').trim({ ' ', '\t' });
        }
        catch (std::runtime_error& err) {
            err_caught = true;
            REQUIRE(err.what() == std::string(err_preamble + " Offending characters: '\"'."));
        }

        REQUIRE(err_caught);
    }

    SECTION("Multiple offenders") {
        try {
            format.delimiter({ ',', '\t', ' ' }).quote('"').trim({ ' ', '\t' });
        }
        catch (std::runtime_error& err) {
            err_caught = true;
            REQUIRE(err.what() == std::string(err_preamble + " Offending characters: '\t', ' '."));
        }

        REQUIRE(err_caught);
    }
}