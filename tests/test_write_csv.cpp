#include <stdio.h> // For remove()
#include <sstream>
#include <queue>
#include <list>
#include "catch.hpp"
#include "csv.hpp"

using namespace csv;
using std::queue;
using std::vector;
using std::string;

#ifndef __clang__
TEST_CASE("Numeric Converter Tests", "[test_convert_number]") {
    // Large numbers: integer larger than uint64 capacity
    REQUIRE(csv::internals::to_string(200000000000000000000.0) == "200000000000000000000.0");
    REQUIRE(csv::internals::to_string(310000000000000000000.0) == "310000000000000000000.0");

    // Test setting precision
    REQUIRE(csv::internals::to_string(1.234) == "1.23400");
    REQUIRE(csv::internals::to_string(20.0045) == "20.00450");

    set_decimal_places(2);
    REQUIRE(csv::internals::to_string(1.234) == "1.23");

    // Reset
    set_decimal_places(5);
}
#endif

TEST_CASE("Basic CSV Writing Cases", "[test_csv_write]") {
    std::stringstream output, correct;
    auto writer = make_csv_writer(output);

    SECTION("Escaped Comma") {
        writer << std::array<std::string, 1>({ "Furthermore, this should be quoted." });
        correct << "\"Furthermore, this should be quoted.\"";
    }

    SECTION("Quote Escape") {
        writer << std::array<std::string, 1>({ "\"What does it mean to be RFC 4180 compliant?\" she asked." });
        correct << "\"\"\"What does it mean to be RFC 4180 compliant?\"\" she asked.\"";
    }

    SECTION("Newline Escape") {
        writer << std::array<std::string, 1>({ "Line 1\nLine2" });
        correct << "\"Line 1\nLine2\"";
    }

    SECTION("Leading and Trailing Quote Escape") {
        writer << std::array<std::string, 1>({ "\"\"" });
        correct << "\"\"\"\"\"\"";
    }

    SECTION("Quote Minimal") {
        writer << std::array<std::string, 1>({ "This should not be quoted" });
        correct << "This should not be quoted";
    }

    correct << std::endl;
    REQUIRE(output.str() == correct.str());
}

TEST_CASE("CSV Quote All", "[test_csv_quote_all]") {
    std::stringstream output, correct;
    auto writer = make_csv_writer(output, false);

    writer << std::array<std::string, 1>({ "This should be quoted" });
    correct << "\"This should be quoted\"" << std::endl;

    REQUIRE(output.str() == correct.str());
}

//! [CSV Writer Example]
TEMPLATE_TEST_CASE("CSV/TSV Writer - operator <<", "[test_csv_operator<<]",
    std::vector<std::string>, std::deque<std::string>, std::list<std::string>) {
    std::stringstream output, correct_comma, correct_tab;

    // Build correct strings
    correct_comma << "A,B,C" << std::endl << "\"1,1\",2,3" << std::endl;
    correct_tab << "A\tB\tC" << std::endl << "1,1\t2\t3" << std::endl;

    // Test input
    auto test_row_1 = TestType({ "A", "B", "C" }),
        test_row_2 = TestType({ "1,1", "2", "3" });

    SECTION("CSV Writer") {
        auto csv_writer = make_csv_writer(output);
        csv_writer << test_row_1 << test_row_2;

        REQUIRE(output.str() == correct_comma.str());
    }

    SECTION("TSV Writer") {
        auto tsv_writer = make_tsv_writer(output);
        tsv_writer << test_row_1 << test_row_2;

        REQUIRE(output.str() == correct_tab.str());
    }
}
//! [CSV Writer Example]

//! [CSV Writer Tuple Example]
struct Time {
    std::string hour;
    std::string minute;

    operator std::string() const {
        std::string ret = hour;
        ret += ":";
        ret += minute;
        
        return ret;
    }
};

#ifndef __clang__
TEST_CASE("CSV Tuple", "[test_csv_tuple]") {
    #ifdef CSV_HAS_CXX17
    Time time = { "5", "30" };
    #else
    std::string time = "5:30";
    #endif
    std::stringstream output, correct_output;
    auto csv_writer = make_csv_writer(output);

    csv_writer << std::make_tuple("One", 2, "Three", 4.0, time)
        << std::make_tuple("One", (short)2, "Three", 4.0f, time)
        << std::make_tuple(-1, -2.0)
        << std::make_tuple(20.2, -20.3, -20.123)
        << std::make_tuple(0.0, 0.0f, 0);

    correct_output << "One,2,Three,4.0,5:30" << std::endl
        << "One,2,Three,4.0,5:30" << std::endl
        << "-1,-2.0" << std::endl
        << "20.19999,-20.30000,-20.12300" << std::endl
        << "0.0,0.0,0" << std::endl;

    REQUIRE(output.str() == correct_output.str());
}
#endif
//! [CSV Writer Tuple Example]
