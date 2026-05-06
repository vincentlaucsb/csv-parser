#include <catch2/catch_all.hpp>
#include "internal/basic_csv_parser.hpp"
#include "internal/csv_row.hpp"
#include "internal/stream_parser.hpp"

#include <deque>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace csv;
using namespace csv::internals;

static std::vector<CSVRow> parse_raw_rows(
    const std::string& csv_text,
    const WhitespaceMap& ws_flags = WhitespaceMap()
) {
    std::stringstream csv(csv_text);
    std::vector<CSVRow> rows;

    StreamParser<std::stringstream> parser(
        csv,
        internals::make_parse_flags(',', '"'),
        ws_flags
    );
    parser.set_output(rows);
    parser.next();

    return rows;
}

TEST_CASE("Basic CSV Parse Test", "[raw_csv_parse]") {
    auto rows = parse_raw_rows("A,B,C\r\n"
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3");

    REQUIRE(rows.size() == 4);
    REQUIRE(rows[0][0] == "A");
    REQUIRE(rows[0][1] == "B");
    REQUIRE(rows[0][2] == "C");
    REQUIRE(rows[0].size() == 3);

    REQUIRE(rows[1][0] == "123");
    REQUIRE(rows[1][1] == "234");
    REQUIRE(rows[1][2] == "345");
    REQUIRE(rows[1].size() == 3);

    REQUIRE(rows[2][0] == "1");
    REQUIRE(rows[2][1] == "2");
    REQUIRE(rows[2][2] == "3");
    REQUIRE(rows[2].size() == 3);

    REQUIRE(rows[3][0] == "1");
    REQUIRE(rows[3][1] == "2");
    REQUIRE(rows[3][2] == "3");
    REQUIRE(rows[3].size() == 3);
}

TEST_CASE("Raw parser can emit rows into a vector sink", "[raw_csv_parse]") {
    std::stringstream csv(
        "A,B,C\n"
        "1,2,3\n"
        "4,5,6\n"
    );

    std::vector<CSVRow> parsed_rows;
    StreamParser<std::stringstream> parser(
        csv,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    parser.set_output(parsed_rows);
    parser.next();

    REQUIRE(parsed_rows.size() == 3);
    REQUIRE(parsed_rows[0][0] == "A");
    REQUIRE(parsed_rows[1][1] == "2");
    REQUIRE(parsed_rows[2][2] == "6");
}

TEST_CASE("Row collection inspect peeks queued rows under one lock", "[raw_csv_parse][row_deque]") {
    auto parsed_rows = parse_raw_rows(
        "A,B\n"
        "1,2\n"
    );
    RowCollection rows;
    rows.push_back(std::move(parsed_rows[0]));
    rows.push_back(std::move(parsed_rows[1]));

    rows.inspect([](const RowQueueInspectionView<CSVRow>& queued) {
        REQUIRE(queued.size() == 2);
        REQUIRE(queued[0][0] == "A");
        REQUIRE(queued[1][1] == "2");
    });

    REQUIRE(rows.size() == 2);
}

TEST_CASE("Row collection append_rows preserves order and ignores empty batches", "[raw_csv_parse][row_deque]") {
    auto parsed_rows = parse_raw_rows(
        "A,B\n"
        "1,2\n"
        "3,4\n"
    );
    RowCollection rows;

    rows.append_rows(std::vector<CSVRow>());
    REQUIRE(rows.empty());
    REQUIRE(rows.size() == 0);

    rows.append_rows(std::move(parsed_rows));

    rows.inspect([](const RowQueueInspectionView<CSVRow>& queued) {
        REQUIRE(queued.size() == 3);
        REQUIRE(queued[0][0] == "A");
        REQUIRE(queued[1][0] == "1");
        REQUIRE(queued[2][1] == "4");
    });

    REQUIRE_FALSE(rows.empty());
    REQUIRE(rows.pop_front()[0] == "A");
    REQUIRE(rows.pop_front()[0] == "1");
    REQUIRE(rows.pop_front()[0] == "3");
    REQUIRE(rows.empty());
}

TEST_CASE("Row collection pop_front and drain_front cross batch boundaries", "[raw_csv_parse][row_deque]") {
    auto first_batch = parse_raw_rows(
        "A,B\n"
        "1,2\n"
    );
    auto second_batch = parse_raw_rows(
        "3,4\n"
        "5,6\n"
    );

    RowCollection rows;
    rows.append_rows(std::move(first_batch));
    rows.append_rows(std::move(second_batch));
    REQUIRE(rows.size() == 4);

    REQUIRE(rows.pop_front()[0] == "A");
    REQUIRE(rows.size() == 3);

    std::vector<CSVRow> drained;
    const size_t drained_count = rows.drain_front(drained, 3);

    REQUIRE(drained_count == 3);
    REQUIRE(drained.size() == 3);
    REQUIRE(drained[0][0] == "1");
    REQUIRE(drained[1][0] == "3");
    REQUIRE(drained[2][0] == "5");
    REQUIRE(rows.empty());
}

TEST_CASE("Row collection drain_front preserves partial batch remainders", "[raw_csv_parse][row_deque]") {
    auto first_batch = parse_raw_rows(
        "A,B\n"
        "1,2\n"
        "3,4\n"
    );
    auto second_batch = parse_raw_rows(
        "5,6\n"
        "7,8\n"
    );

    RowCollection rows;
    rows.append_rows(std::move(first_batch));
    rows.append_rows(std::move(second_batch));

    std::vector<CSVRow> drained;
    REQUIRE(rows.drain_front(drained, 0) == 0);
    REQUIRE(rows.size() == 5);

    REQUIRE(rows.drain_front(drained, 2) == 2);
    REQUIRE(drained.size() == 2);
    REQUIRE(drained[0][0] == "A");
    REQUIRE(drained[1][0] == "1");
    REQUIRE(rows.size() == 3);

    rows.inspect([](const RowQueueInspectionView<CSVRow>& queued) {
        REQUIRE(queued.size() == 3);
        REQUIRE(queued[0][0] == "3");
        REQUIRE(queued[1][0] == "5");
        REQUIRE(queued[2][0] == "7");
    });

    std::vector<CSVRow> rest;
    REQUIRE(rows.drain_front(rest, 10) == 3);
    REQUIRE(rest.size() == 3);
    REQUIRE(rest[0][0] == "3");
    REQUIRE(rest[1][0] == "5");
    REQUIRE(rest[2][0] == "7");
    REQUIRE(rows.empty());
}

TEST_CASE("Raw parser can parse a caller-owned chunk directly", "[raw_csv_parse]") {
    std::stringstream unused_source;
    std::vector<CSVRow> parsed_rows;
    CSVRowOutput sink(parsed_rows);

    StreamParser<std::stringstream> parser(
        unused_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    auto chunk = std::make_shared<std::string>(
        "left,right\n"
        "\"a,b\",2\n"
    );

    const auto result = parser.parse_chunk(*chunk, chunk, sink);

    REQUIRE(result.complete_prefix_length == chunk->size());
    REQUIRE_FALSE(result.ending_state.quote_escape);
    REQUIRE_FALSE(result.ending_state.pending_quote);
    REQUIRE(parsed_rows.size() == 2);
    REQUIRE(parsed_rows[0][0] == "left");
    REQUIRE(parsed_rows[1][0] == "a,b");
    REQUIRE(parsed_rows[1][1] == "2");
}

TEST_CASE("CSVRow raw_str uses record boundaries rather than newline search", "[raw_csv_parse]") {
    std::stringstream unused_source;
    std::vector<CSVRow> parsed_rows;
    CSVRowOutput sink(parsed_rows);

    StreamParser<std::stringstream> parser(
        unused_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    auto chunk = std::make_shared<std::string>(
        "id,text,status\r\n"
        "1,\"hello\nworld\",ok\r\n"
        "2,plain,ok\r\n"
    );

    const auto result = parser.parse_chunk(*chunk, chunk, sink);

    REQUIRE(result.complete_prefix_length == chunk->size());
    REQUIRE(parsed_rows.size() == 3);
    REQUIRE(parsed_rows[0].raw_str() == "id,text,status");
    REQUIRE(parsed_rows[1].raw_str() == "1,\"hello\nworld\",ok");
    REQUIRE(parsed_rows[2].raw_str() == "2,plain,ok");
}

TEST_CASE("Test Quote Escapes", "[test_parse_quote_escape]") {
    auto rows = parse_raw_rows(""
        "\"A\",\"B\",\"C\"\r\n"   // Quoted fields w/ no escapes
        "123,\"234,345\",456\r\n" // Escaped comma
        "1,\"2\"\"3\",4\r\n"      // Escaped quote
        "1,\"23\"\"34\",5\r\n"      // Another escaped quote
        "1,\"\",2\r\n");           // Empty Field

    REQUIRE(rows.size() == 5);
    REQUIRE(rows[0][0] == "A");
    REQUIRE(rows[0][1] == "B");
    REQUIRE(rows[0][2] == "C");
    REQUIRE(rows[0].size() == 3);

    REQUIRE(rows[1][0] == "123");
    REQUIRE(rows[1][1] == "234,345");
    REQUIRE(rows[1][2] == "456");
    REQUIRE(rows[1].size() == 3);

    REQUIRE(rows[2][0] == "1");
    REQUIRE(rows[2][1] == "2\"3");
    REQUIRE(rows[2][2] == "4");
    REQUIRE(rows[2].size() == 3);

    REQUIRE(rows[3][0] == "1");
    REQUIRE(rows[3][1] == "23\"34");
    REQUIRE(rows[3][2] == "5");
    REQUIRE(rows[3].size() == 3);

    REQUIRE(rows[4][0] == "1");
    REQUIRE(rows[4][1] == "");
    REQUIRE(rows[4][2] == "2");
    REQUIRE(rows[4].size() == 3);
}

TEST_CASE("Parser DFA state can be seeded and reported", "[raw_csv_parse][dfa_state]") {
    SECTION("Reports unfinished quoted field at chunk end") {
        std::stringstream csv("A,\"unfinished");
        std::vector<CSVRow> rows;

        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::WhitespaceMap()
        );

        parser.set_output(rows);
        parser.next();

        const auto state = parser.ending_state();
        REQUIRE(state.quote_escape);
        REQUIRE_FALSE(state.pending_quote);
    }

    SECTION("Reports pending quote when chunk ends on quoted-field quote") {
        std::stringstream csv("\"abc\"z\n");
        std::vector<CSVRow> rows;

        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::WhitespaceMap()
        );

        parser.set_output(rows);
        parser.next(5);

        const auto state = parser.ending_state();
        REQUIRE(state.quote_escape);
        REQUIRE(state.pending_quote);
        REQUIRE(rows.empty());
    }

    SECTION("Seeded quoted state treats delimiters and newlines as field content") {
        std::stringstream csv("alpha\nbeta\",tail\n");
        std::vector<CSVRow> rows;

        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::WhitespaceMap()
        );

        parser.set_output(rows);
        parser.reset_with_initial_state(true);
        parser.next();

        REQUIRE_FALSE(parser.ending_state().quote_escape);
        REQUIRE_FALSE(parser.ending_state().pending_quote);
        REQUIRE(rows.size() == 1);

        const auto row = rows[0];
        REQUIRE(row.size() == 2);
        REQUIRE(row[0] == "alpha\nbeta");
        REQUIRE(row[1] == "tail");
    }
}

inline std::vector<std::string> make_whitespace_test_cases() {
    std::vector<std::string> test_cases = {};
    std::stringstream ss;

    ss << "1, two,3" << std::endl
        << "4, ,5" << std::endl
        << " ,6, " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    // Lots of Whitespace
    ss << "1, two,3" << std::endl
        << "4,                    ,5" << std::endl
        << "         ,6,       " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    // Same as above but there's whitespace around 6
    ss << "1, two,3" << std::endl
        << "4,                    ,5" << std::endl
        << "         , 6 ,       " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    // Tabs
    ss << "1, two,3" << std::endl
        << "4, \t ,5" << std::endl
        << "\t\t\t\t\t ,6, \t " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    return test_cases;
}

TEST_CASE("Test Parser Whitespace Trimming", "[test_csv_trim]") {
    auto row_str = GENERATE(as<std::string> {},
        "A,B,C\r\n" // Header row
        "123,\"234\n,345\",456\r\n",

        // Random spaces
        "A,B,C\r\n"
        "   123,\"234\n,345\",    456\r\n",

        // Random spaces + tabs
        "A,B,C\r\n"
        "\t\t   123,\"234\n,345\",    456\r\n",

        // Spaces in quote escaped field
        "A,B,C\r\n"
        "\t\t   123,\"   234\n,345  \t\",    456\r\n",

        // Spaces in one header column
        "A,B,        C\r\n"
        "123,\"234\n,345\",456\r\n",

        // Random spaces + tabs in header
        "\t A,  B\t,     C\r\n"
        "123,\"234\n,345\",456\r\n",

        // Random spaces in header + data
        "A,B,        C\r\n"
        "123,\"234\n,345\",  456\r\n"
    );

    SECTION("Parse Test") {
        using namespace std;

        auto rows = parse_raw_rows(
            row_str,
            internals::make_ws_flags({ ' ', '\t' })
        );

        auto header = rows[0];
        REQUIRE(vector<string>(header) == vector<string>(
            { "A", "B", "C" }));

        auto row = rows[1];
        REQUIRE(vector<string>(row) ==
            vector<string>({ "123", "234\n,345", "456" }));
        REQUIRE(row[0] == "123");
        REQUIRE(row[1] == "234\n,345");
        REQUIRE(row[2] == "456");
    }
}

TEST_CASE("Test Parser Whitespace Trimming w/ Empty Fields", "[test_raw_ws_trim]") {
    auto csv_string = GENERATE(from_range(make_whitespace_test_cases()));

    SECTION("Parse Test") {
        auto rows = parse_raw_rows(
            csv_string,
            internals::make_ws_flags({ ' ', '\t' })
        );

        size_t row_no = 0;
        for (auto& row : rows) {
            switch (row_no) {
            case 0:
                REQUIRE(row[0].get<uint32_t>() == 1);
                REQUIRE(row[1].get<std::string>() == "two");
                REQUIRE(row[2].get<uint32_t>() == 3);
                break;

            case 1:
                REQUIRE(row[0].get<uint32_t>() == 4);
                REQUIRE(row[1].is_null());
                REQUIRE(row[2].get<uint32_t>() == 5);
                break;

            case 2:
                REQUIRE(row[0].is_null());
                REQUIRE(row[1].get<uint32_t>() == 6);
                REQUIRE(row[2].is_null());
                break;

            case 3:
                REQUIRE(row[0].get<uint32_t>() == 7);
                REQUIRE(row[1].get<uint32_t>() == 8);
                REQUIRE(row[2].get<uint32_t>() == 9);
                break;
            }

            row_no++;
        }
    }
}
