/** @file
 *  Tests for CSV parsing
 */

#include <stdio.h> // remove()
#include <sstream>
#include "catch.hpp"
#include "csv.hpp"

using namespace csv;
using std::vector;
using std::string;

//
// guess_delim()
//
TEST_CASE("guess_delim() Test - Pipe", "[test_guess_pipe]") {
    CSVGuessResult format = guess_format(
        "./tests/data/real_data/2009PowerStatus.txt");
    REQUIRE(format.delim == '|');
    REQUIRE(format.header_row == 0);
}

TEST_CASE("guess_delim() Test - Semi-Colon", "[test_guess_scolon]") {
    CSVGuessResult format = guess_format(
        "./tests/data/real_data/YEAR07_CBSA_NAC3.txt");
    REQUIRE(format.delim == ';');
    REQUIRE(format.header_row == 0);
}

TEST_CASE("guess_delim() Test - CSV with Comments", "[test_guess_comment]") {
    CSVGuessResult format = guess_format(
        "./tests/data/fake_data/ints_comments.csv");
    REQUIRE(format.delim == ',');
    REQUIRE(format.header_row == 5);
}