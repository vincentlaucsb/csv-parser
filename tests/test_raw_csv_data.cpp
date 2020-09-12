#include "catch.hpp"
#include "csv.hpp"

using namespace csv;

TEST_CASE("Basic CSV Parse Test", "[raw_csv_parse]") {
    std::string csv = "A,B,C\r\n" // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3\r\n";

    BasicCSVParser parser(std::move(csv));
    std::deque<RawCSVRow> rows;
    bool quote_escape = false;
    
    parser.parse(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap(),
        quote_escape,
        rows
    );

    auto row = rows.front();
    REQUIRE(row.get_field(0) == "A");
    REQUIRE(row.get_field(1) == "B");
    REQUIRE(row.get_field(2) == "C");
    REQUIRE(row.row_length == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "123");
    REQUIRE(row.get_field(1) == "234");
    REQUIRE(row.get_field(2) == "345");
    REQUIRE(row.row_length == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "1");
    REQUIRE(row.get_field(1) == "2");
    REQUIRE(row.get_field(2) == "3");
    REQUIRE(row.row_length == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "1");
    REQUIRE(row.get_field(1) == "2");
    REQUIRE(row.get_field(2) == "3");
    REQUIRE(row.row_length == 3);
}

TEST_CASE("Test Quote Escapes", "[test_parse_quote_escape]") {
    std::string csv = ""
        "\"A\",\"B\",\"C\"\r\n"   // Quoted fields w/ no escapes
        "123,\"234,345\",456\r\n" // Escaped comma
        "1,\"2\"\"3\",4\r\n"      // Escaped quote
        "1,2,3\r\n";

    BasicCSVParser parser(std::move(csv));
    std::deque<RawCSVRow> rows;
    bool quote_escape = false;

    parser.parse(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap(),
        quote_escape,
        rows
    );

    auto row = rows.front();
    REQUIRE(row.get_field(0) == "A");
    REQUIRE(row.get_field(1) == "B");
    REQUIRE(row.get_field(2) == "C");
    REQUIRE(row.row_length == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "123");
    REQUIRE(row.get_field(1) == "234,345");
    REQUIRE(row.get_field(2) == "456");
    REQUIRE(row.row_length == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "1");
    REQUIRE(row.get_field(1) == "2\"3");
    REQUIRE(row.get_field(2) == "4");
    REQUIRE(row.row_length == 3);
}