#include <catch2/catch_all.hpp>
#include "internal/basic_csv_parser.hpp"
#include "internal/csv_row.hpp"
#include "internal/stream_parser.hpp"

#include <sstream>

using namespace csv;
using namespace csv::internals;
using RowCollectionTest = ThreadSafeDeque<CSVRow>;

TEST_CASE("Basic CSV Parse Test", "[raw_csv_parse]") {
    std::stringstream csv("A,B,C\r\n"
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3");

    RowCollectionTest rows;

    StreamParser<std::stringstream> parser(
        csv,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    parser.set_output(rows);
    parser.next();

    auto row = rows.front();
    REQUIRE(row[0] == "A");
    REQUIRE(row[1] == "B");
    REQUIRE(row[2] == "C");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "123");
    REQUIRE(row[1] == "234");
    REQUIRE(row[2] == "345");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "2");
    REQUIRE(row[2] == "3");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "2");
    REQUIRE(row[2] == "3");
    REQUIRE(row.size() == 3);
}

TEST_CASE("Raw parser can emit rows into a vector sink", "[raw_csv_parse]") {
    std::stringstream csv(
        "A,B,C\n"
        "1,2,3\n"
        "4,5,6\n"
    );

    std::vector<CSVRow> parsed_rows;
    CSVRowOutput sink(parsed_rows);

    StreamParser<std::stringstream> parser(
        csv,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    parser.set_output(sink);
    parser.next();

    REQUIRE(parsed_rows.size() == 3);
    REQUIRE(parsed_rows[0][0] == "A");
    REQUIRE(parsed_rows[1][1] == "2");
    REQUIRE(parsed_rows[2][2] == "6");
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
    std::stringstream csv(""
        "\"A\",\"B\",\"C\"\r\n"   // Quoted fields w/ no escapes
        "123,\"234,345\",456\r\n" // Escaped comma
        "1,\"2\"\"3\",4\r\n"      // Escaped quote
        "1,\"23\"\"34\",5\r\n"      // Another escaped quote
        "1,\"\",2\r\n");           // Empty Field

    RowCollectionTest rows;

    StreamParser<std::stringstream> parser(
        csv,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    parser.set_output(rows);
    parser.next();

    auto row = rows.front();
    REQUIRE(row[0] == "A");
    REQUIRE(row[1] == "B");
    REQUIRE(row[2] == "C");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "123");
    REQUIRE(row[1] == "234,345");
    REQUIRE(row[2] == "456");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "2\"3");
    REQUIRE(row[2] == "4");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "23\"34");
    REQUIRE(row[2] == "5");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "");
    REQUIRE(row[2] == "2");
    REQUIRE(row.size() == 3);
}

TEST_CASE("Parser DFA state can be seeded and reported", "[raw_csv_parse][dfa_state]") {
    SECTION("Reports unfinished quoted field at chunk end") {
        std::stringstream csv("A,\"unfinished");
        RowCollectionTest rows;

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
        RowCollectionTest rows;

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
        RowCollectionTest rows;

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

        const auto row = rows.front();
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

        RowCollectionTest rows;

        auto csv = std::stringstream(row_str);
        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::make_ws_flags({ ' ', '\t' })
        );

        parser.set_output(rows);
        parser.next();

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
        RowCollectionTest rows;

        auto csv = std::stringstream(csv_string);
        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::make_ws_flags({ ' ', '\t' })
        );

        parser.set_output(rows);

        parser.next();

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
