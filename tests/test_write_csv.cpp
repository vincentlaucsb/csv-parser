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

/**
TEST_CASE("CSV Comma Escape", "[test_csv_comma]") {
    std::string input = "Furthermore, this should be quoted.";
    std::string correct = "\"Furthermore, this should be quoted.\"";

    REQUIRE(csv_escape<>(input) == correct);
}

TEST_CASE("CSV Quote Escape", "[test_csv_quote]") {
    std::string input = "\"What does it mean to be RFC 4180 compliant?\" she asked.";
    std::string correct = "\"\"\"What does it mean to be RFC 4180 compliant?\"\" she asked.\"";

    REQUIRE(csv_escape<>(input) == correct);
}

TEST_CASE("CSV Quote Minimal", "[test_csv_quote_min]") {
    std::string input = "This should not be quoted";
    REQUIRE(csv_escape<>(input) == input);
}

TEST_CASE("CSV Quote All", "[test_csv_quote_all]") {
    std::string input = "This should be quoted";
    std::string correct = "\"This should be quoted\"";
    REQUIRE(csv_escape<>(input, false) == correct);
}
*/

TEST_CASE("to_string - Double", "[test_to_string_double]") {
    double value = 20.2;
    double neg_value = -20.2;

    REQUIRE(internals::to_string(neg_value) == "-20.19999");
}

TEST_CASE("CSV to Stringstream", "[test_csv_sstream1]") {
    std::stringstream out, correct;

    // Build correct string
    correct << "A,B,C" << std::endl << "\"1,1\",2,3" << std::endl;

    queue<vector<string>> q;
    q.push({ "A", "B", "C" });
    q.push({ "1,1", "2", "3" });

    auto writer = make_csv_writer(out);
    for (; !q.empty(); q.pop())
        writer.write_row(q.front());

    REQUIRE(out.str() == correct.str());
}

struct Time {
    std::string hour;
    std::string minute;
};

namespace csv {
    namespace internals {
        std::string to_string(Time time) {
            return time.hour + ":" + time.minute;
        }
    }
}

TEST_CASE("CSV Tuple", "[test_csv_tuple]") {
    Time time = { "5", "30" };
    std::stringstream output, correct_output;
    auto csv_writer = make_csv_writer(output);

    csv_writer << std::make_tuple("One", 2, "Three", 4.0, time)
        << std::make_tuple("One", (short)2, "Three", 4.0f, time);

    for (size_t i = 0; i < 2; i++) {
        correct_output << "One,2,Three,4.0,5:30" << std::endl;
    }

    REQUIRE(output.str() == correct_output.str());
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