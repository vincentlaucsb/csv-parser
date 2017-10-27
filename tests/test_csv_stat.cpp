# define CATCH_CONFIG_MAIN
# include "catch.hpp"
# include "csv_stat.cpp"
# include <string>
# include <vector>

using namespace csv_parser;

TEST_CASE( "Test Calculating Statistics from Direct Input", 
    "[read_csv_stat_direct]" ) {
    // Header on first row
    CSVStat reader(",", "\"");
    reader.set_col_names({"A", "B", "C"});
    std::string int_str;
    std::string int_list;
    
    for (int i = 1; i < 101; i++) {
        int_str = std::to_string(i);
        int_list = int_str + "," + int_str + "," + int_str + "\r\n";
        reader.feed(int_list);
    }
    
    reader.end_feed();
    reader.calc();
    
    // Expected Results
    std::vector<long double> means = { 50.5, 50.5, 50.5 };
    std::vector<long double> mins = { 1, 1, 1 };
    std::vector<long double> maxes = { 100, 100, 100 };
    
    REQUIRE( reader.get_mins() == mins );
    REQUIRE( reader.get_maxes() == maxes );
    REQUIRE( reader.get_mean() == means );
    REQUIRE( ceil(reader.get_variance()[0]) == 842 );
    
    // Confirm column at pos 0 has 100 integers (type 2)
    REQUIRE( reader.get_dtypes()[0][2] == 100 );
}

TEST_CASE( "Test Calculating Statistics", "[read_csv_stat]" ) {
    // Header on first row
    CSVStat reader(",", "\"", 0);
    reader.read_csv("./tests/data/fake_data/ints.csv");
    reader.calc();
    
    // Expected Results
    std::vector<long double> means = {
        50.5, 50.5, 50.5, 50.5, 50.5,
        50.5, 50.5, 50.5, 50.5, 50.5
    };
    
    REQUIRE( reader.get_mean() == means );
    REQUIRE( reader.get_mins()[0] == 1 );
    REQUIRE( reader.get_maxes()[0] == 100 );
    REQUIRE( ceil(reader.get_variance()[0]) == 842 );
}

TEST_CASE( "Test Calculating Statistics (Line Feed Record-Separated)",
    "[read_csv_stat2]" ) {
    // Header on first row
    CSVStat reader(",", "\"", 0);
    reader.read_csv("./tests/data/fake_data/ints_newline_sep.csv");
    reader.calc();
    
    // Expected Results
    std::vector<long double> means = {
        50.5, 50.5, 50.5, 50.5, 50.5,
        50.5, 50.5, 50.5, 50.5, 50.5
    };
    
    REQUIRE( reader.get_mean() == means );
    REQUIRE( reader.get_mins()[0] == 1 );
    REQUIRE( reader.get_maxes()[0] == 100 );
    REQUIRE( ceil(reader.get_variance()[0]) == 842 );
}