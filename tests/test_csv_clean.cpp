# define CATCH_CONFIG_MAIN
# include "csv_parser.h"
# include "catch.hpp"
# include <string>

using namespace csv_parser;

TEST_CASE( "Test Calculating Statistics", "[csv_clean]" ) {
    // Header on first row
    CSVCleaner reader(",", "\"", 0);
    reader.read_csv("./tests/data/fake_data/ints.csv");
    reader.to_csv("./tests/data/fake_data/ints2.csv");
    
    // 100 ints (type 2) in all columns
    for (int i = 0; i < 10; i++) {
        REQUIRE( reader.get_dtypes()[i][2] == 100 );
    }
}

TEST_CASE( "Test Line Skipping", "[csv_skiplines]" ) {
    // Header on first row
    CSVCleaner reader(",", "\"", 0);
    reader.read_csv("./tests/data/fake_data/ints_skipline.csv");
    
    // Minimal quoting + skip one line
    reader.to_csv("./tests/data/fake_data/ints_skipline2.csv", true, 1);
    
    // 100 ints (type 2) in all columns
    for (int i = 0; i < 10; i++) {
        REQUIRE( reader.get_dtypes()[i][2] == 100 );
    }
}

TEST_CASE( "Test Converting Tab Delimited File", "[tsv_clean]" ) {
    // Header on first row
    CSVCleaner reader("\t", "\"", 0);
    reader.read_csv("./tests/data/real_data/2016_Gaz_place_national.txt");
    reader.to_csv("./tests/data/real_data/2016_Gaz_place_national.csv");
    
    // Calculate some statistics on the cleaned CSV to verify it's good
    CSVStat stats(",", "\"", 0);
    stats.read_csv("./tests/data/real_data/2016_Gaz_place_national.csv");
    stats.calc();
    
    // 10 = INTPTLAT; 11 = INTPTLONG
    REQUIRE(ceil(stats.get_mean()[10]) == 39);
    REQUIRE(ceil(stats.get_mean()[10]) == 39);
}

TEST_CASE( "Test CSV Merge", "[csv_merge]") {
    merge_csv("StormEvents.csv",
        {"./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2014_c20170718.csv",
        "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2015_c20170718.csv",
        "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2016_c20170816.csv",
        "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2017_c20170816.csv"});
}