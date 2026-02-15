/** @file
 *  Tests for CSV parsing
 */

#include <catch2/catch_all.hpp>
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

TEST_CASE("guess_delim() Test - Header Wider Than Data (Issue #283)", "[test_guess_wide_header]") {
    // This test validates the fix for issue #283
    // When the header has MORE columns than data rows (4 vs 3), the parser
    // should use the first row as the header (because first_row_length >= mode_length)
    //
    // This commonly occurs with:
    // - Optional/sparse columns
    // - Trailing delimiters in headers
    // - Schema evolution (new columns added but old data not backfilled)
    
    CSVGuessResult format = guess_format("./tests/data/fake_data/wide_header.csv");
    REQUIRE(format.delim == ';');
    REQUIRE(format.header_row == 0);
    
    CSVReader reader("./tests/data/fake_data/wide_header.csv");
    auto col_names = reader.get_col_names();

    // With fix for #283: First row (a;b;c;d) has 4 columns >= mode of 3 columns,
    // so it's correctly identified as the header
    REQUIRE(col_names.size() == 4);
    REQUIRE(col_names[0] == "a");
    REQUIRE(col_names[1] == "b");
    REQUIRE(col_names[2] == "c");
    REQUIRE(col_names[3] == "d");
}

TEST_CASE("guess_delim() Test - Comments Before Header", "[test_guess_comments_before_header]") {
    // Verify the heuristic still handles comment lines correctly
    // When first row is SHORTER than mode, use first row with mode length as header
    
    CSVGuessResult format = guess_format("./tests/data/fake_data/comments_before_header.csv");
    REQUIRE(format.delim == ';');
    REQUIRE(format.header_row == 2);  // Row 2 is "a;b;c"
    
    CSVReader reader("./tests/data/fake_data/comments_before_header.csv");
    auto col_names = reader.get_col_names();
    
    REQUIRE(col_names.size() == 3);
    REQUIRE(col_names[0] == "a");
    REQUIRE(col_names[1] == "b");
    REQUIRE(col_names[2] == "c");
}