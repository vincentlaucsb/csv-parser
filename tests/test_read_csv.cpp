# define CATCH_CONFIG_MAIN
# include "catch.hpp"
# include "csv_parser.hpp"
# include <string>
# include <vector>

using namespace csvmorph;

TEST_CASE( "Test Reading CSV From Direct Input", "[read_csv_direct]" ) {
    std::string csv_string1("123,234,345\r\n"
                            "1,2,3\r\n"
                            "1,2,3");
                            ;
    std::vector<std::string> col_names = {"A", "B", "C"};
    
    // Feed Strings
    CSVReader reader(",", "\"", col_names);
    reader.feed(csv_string1);
    reader.end_feed();
    
    // Expected Results
    std::vector<std::string> first_row = {"123", "234", "345"};
    REQUIRE( reader.pop() == first_row );
}

TEST_CASE( "Test Comma Escape", "[read_csv_direct2]" ) {
    std::string csv_string2 = ("123,\"234,345\",456\r\n"
                               "1,2,3\r\n"
                               "1,2,3");
    std::vector<std::string> col_names = {"A", "B", "C"};
    
    // Feed Strings
    CSVReader reader(",", "\"", col_names);
    reader.feed(csv_string2);
    reader.end_feed();
    
    // Expected Results
    std::vector<std::string> first_row = {"123", "234,345", "456"};
    REQUIRE( reader.pop() == first_row );
}

// std::string psv_string = "123|\"234,345\"|456\r\n1|2|3\r\n1|2|3";