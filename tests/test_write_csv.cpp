#include "catch.hpp"
#include "csv_parser.h"
#include <string>
#include <vector>

using namespace csv_parser;

TEST_CASE("CSV Comma Escape", "[test_csv_comma]") {
    std::string input = "Furthermore, this should be quoted.";
    std::string correct = "\"Furthermore, this should be quoted.\"";

    REQUIRE(csv_escape(input) == correct);
}

TEST_CASE("CSV Quote Escape", "[test_csv_quote]") {
    std::string input = "\"What does it mean to be RFC 4180 compliant?\" she asked.";
    std::string correct = "\"\"\"What does it mean to be RFC 4180 compliant?\"\" she asked.\"";

    REQUIRE(csv_escape(input) == correct);
}

TEST_CASE("CSV Quote Minimal", "[test_csv_quote_min]") {
    std::string input = "This should not be quoted";
    REQUIRE(csv_escape(input) == input);
}

TEST_CASE("CSV Quote All", "[test_csv_quote_all]") {
    std::string input = "This should be quoted";
    std::string correct = "\"This should be quoted\"";
    REQUIRE(csv_escape(input, false) == correct);
}