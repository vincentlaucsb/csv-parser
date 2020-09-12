#include "catch.hpp"
#include "csv.hpp"

using namespace csv;

TEST_CASE("Basic CSV Parse Test", "[raw_csv_parse]") {
    std::string csv = "A,B,C\r\n" // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3";

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
}