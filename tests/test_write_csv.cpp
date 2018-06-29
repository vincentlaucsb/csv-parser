#include <stdio.h> // For remove()
#include <sstream>
#include <queue>
#include "catch.hpp"
#include "csv_parser.hpp"
#include "csv_writer.hpp"

using namespace csv;
using std::queue;
using std::vector;
using std::string;

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

TEST_CASE("CSV/TSV Writer - operator <<", "[test_csv_operator<<]") {
    std::stringstream comma_out, tab_out, correct_comma, correct_tab;

    // Build correct strings
    correct_comma << "A,B,C" << std::endl << "\"1,1\",2,3" << std::endl;
    correct_tab << "A\tB\tC" << std::endl << "1,1\t2\t3" << std::endl;

    auto csv_writer = make_csv_writer(comma_out);
    csv_writer << vector<string>({ "A", "B", "C" })
        << vector<string>({ "1,1", "2", "3" });

    REQUIRE(comma_out.str() == correct_comma.str());

    auto tsv_writer = make_tsv_writer(tab_out);
    tsv_writer << vector<string>({ "A", "B", "C" })
        << vector<string>({ "1,1", "2", "3" });

    REQUIRE(tab_out.str() == correct_tab.str());
}

/*
TEST_CASE("CSV Round Trip", "[test_csv_roundtrip]") {
    const char * test_file = "./tests/temp/round_trip.csv";
    std::ofstream out(test_file);

    queue<vector<string>> q;
    q.push({ "A", "B", "C" });
    q.push({ "D", "E", "F" });
    q.push({ "1,1", "2", "3" });
    q.push({ "4", "5,6", "7" });

    auto writer = make_csv_writer(out);
    for (; !q.empty(); q.pop())
        writer.write_row(q.front());

    CSVReader reader(test_file);
    CSVRow rows;

    REQUIRE(reader.get_col_names() == vector<string>({ "A", "B", "C" }));

    reader.read_row(rows);
    REQUIRE(rows[0].get<std::string>() == "D");

    reader.read_row(rows);
    REQUIRE(rows[0].get<std::string>() == "1,1");
    REQUIRE(rows[1].get<int>() == 2);
    REQUIRE(rows[2].get<int>() == 3);

    reader.read_row(rows);
    REQUIRE(rows[2].get<int>() == 7);

    // Clean-up
    out.close();
    REQUIRE(remove(test_file) == 0);
}
*/