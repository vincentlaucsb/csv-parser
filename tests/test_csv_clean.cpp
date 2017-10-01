# define CATCH_CONFIG_MAIN
# include "catch.hpp"
# include "csv_stat.hpp"
# include <string>

using namespace csvmorph;

TEST_CASE( "Test Calculating Statistics", "[csv_clean]" ) {
    // Header on first row
    CSVCleaner reader(",", "\"", 0);
    reader.read_csv("./tests/data/fake_data/ints.csv");
    reader.to_csv("./tests/data/fake_data/ints2.csv");
    
    // 100 ints in column 1 (type 2)
    REQUIRE( reader.get_dtypes()[0][2] == 100 );
}