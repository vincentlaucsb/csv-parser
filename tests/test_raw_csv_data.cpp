#include "catch.hpp"
#include "internal/basic_csv_parser.hpp"
#include "internal/csv_row.hpp"

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
