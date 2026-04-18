#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include <sstream>

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
/* Ensure no_header() works correctly with delimiter guessing
 * 
 * Reported in: https://github.com/vincentlaucsb/csv-parser/issues/285
 * 
 * When using .no_header() with multiple delimiters (which triggers guessing),
 * the guessing logic was overwriting the header=-1 setting, causing the first
 * data row to be skipped.
 */
TEST_CASE("CSVFormat - no_header() with Delimiter Guessing (Issue #285)", "[csv_format_no_header]") {
    // Verify delimiter guessing preserves no_header() setting
    std::string csv_string = "row\t1\n"
                             "row\t2\n"
                             "row\t3\n";
    
    SECTION("Multiple delimiters + no_header()") {
        CSVFormat format;
        format.delimiter({'\t', ';'})  // Multiple delimiters triggers guessing
              .no_header();             // Should preserve all rows
        
        std::stringstream source(csv_string);
        CSVReader reader(source, format);
        
        std::vector<CSVRow> rows;
        for (auto& row : reader) {
            rows.push_back(row);
        }
        
        // Assert CORRECT behavior: All 3 rows should be present
        REQUIRE(rows.size() == 3);
        REQUIRE(rows[0][0].get<>() == "row");
        REQUIRE(rows[0][1].get<>() == "1");
        REQUIRE(rows[1][0].get<>() == "row");
        REQUIRE(rows[1][1].get<>() == "2");
        REQUIRE(rows[2][0].get<>() == "row");
        REQUIRE(rows[2][1].get<>() == "3");
    }
    
    SECTION("Single delimiter + no_header() - should work") {
        // Verify single delimiter doesn't trigger the bug
        CSVFormat format;
        format.delimiter('\t')  // Single delimiter, no guessing
              .no_header();
        
        std::stringstream source(csv_string);
        CSVReader reader(source, format);
        
        std::vector<CSVRow> rows;
        for (auto& row : reader) {
            rows.push_back(row);
        }
        
        // Single delimiter path should already work correctly
        REQUIRE(rows.size() == 3);
        REQUIRE(rows[0][0].get<>() == "row");
        REQUIRE(rows[0][1].get<>() == "1");
    }
}
