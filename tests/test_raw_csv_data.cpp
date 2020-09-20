#include "catch.hpp"
#include "csv.hpp"

using namespace csv;

internals::WorkItem make_work_item(csv::string_view in) {
    std::unique_ptr<char[]> str(new char[in.size()]);
    auto* buffer = str.get();
    in.copy(buffer, in.size());

    return std::make_pair<>(std::move(str), in.length());
}

TEST_CASE("Basic CSV Parse Test", "[raw_csv_parse]") {
    std::string csv = "A,B,C\r\n" // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3\r\n";

    BasicCSVParser parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    std::deque<CSVRow> rows;
    bool quote_escape = false;
    
    parser.parse(
        csv,
        rows
    );

    auto row = rows.front();
    REQUIRE(row.get_field(0) == "A");
    REQUIRE(row.get_field(1) == "B");
    REQUIRE(row.get_field(2) == "C");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "123");
    REQUIRE(row.get_field(1) == "234");
    REQUIRE(row.get_field(2) == "345");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "1");
    REQUIRE(row.get_field(1) == "2");
    REQUIRE(row.get_field(2) == "3");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "1");
    REQUIRE(row.get_field(1) == "2");
    REQUIRE(row.get_field(2) == "3");
    REQUIRE(row.size() == 3);
}

TEST_CASE("Test Quote Escapes", "[test_parse_quote_escape]") {
    std::string csv = ""
        "\"A\",\"B\",\"C\"\r\n"   // Quoted fields w/ no escapes
        "123,\"234,345\",456\r\n" // Escaped comma
        "1,\"2\"\"3\",4\r\n"      // Escaped quote
        "1,2,3\r\n";

    BasicCSVParser parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    std::deque<CSVRow> rows;
    parser.parse(csv, rows);

    auto row = rows.front();
    REQUIRE(row.get_field(0) == "A");
    REQUIRE(row.get_field(1) == "B");
    REQUIRE(row.get_field(2) == "C");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "123");
    REQUIRE(row.get_field(1) == "234,345");
    REQUIRE(row.get_field(2) == "456");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row.get_field(0) == "1");
    REQUIRE(row.get_field(1) == "2\"3");
    REQUIRE(row.get_field(2) == "4");
    REQUIRE(row.size() == 3);
}

TEST_CASE("Basic Fragment Test", "[raw_csv_fragment]") {
    auto csv_fragments = GENERATE(as<std::vector<std::string>> {},
        std::vector<std::string>({
            "A,B,C\r\n"
            "123,234,345\r\n",
            "1,2,3\r\n"
            "1,2,3\r\n"
        }),
        
        std::vector<std::string>({
            "A,B,C\r\n"
            "123,234,", "345\r\n",
            "1,2,3\r\n"
            "1,2,3\r\n"
        }),
        std::vector<std::string>({
            "\"A\",\"B\",\"C\"\r\n"
            "123,234,", "345\r\n",
            "1,\"2", "\",3\r\n"     // Fragment in middle of quoted field
            "1,2,3\r\n"
        })
    );

    SECTION("Fragment Stitching") {
        BasicCSVParser parser(
            internals::make_parse_flags(',', '"'),
            internals::WhitespaceMap()
        );
        std::deque<CSVRow> rows;
        
        for (auto& frag : csv_fragments) {
            parser.parse(frag, rows);
        }

        auto row = rows.front();
        REQUIRE(row.get_field(0) == "A");
        REQUIRE(row.get_field(1) == "B");
        REQUIRE(row.get_field(2) == "C");
        REQUIRE(row.size() == 3);

        rows.pop_front();
        row = rows.front();
        REQUIRE(row.get_field(0) == "123");
        REQUIRE(row.get_field(1) == "234");
        REQUIRE(row.get_field(2) == "345");
        REQUIRE(row.size() == 3);

        rows.pop_front();
        row = rows.front();
        REQUIRE(row.get_field(0) == "1");
        REQUIRE(row.get_field(1) == "2");
        REQUIRE(row.get_field(2) == "3");
        REQUIRE(row.size() == 3);

        rows.pop_front();
        row = rows.front();
        REQUIRE(row.get_field(0) == "1");
        REQUIRE(row.get_field(1) == "2");
        REQUIRE(row.get_field(2) == "3");
        REQUIRE(row.size() == 3);
    }
}