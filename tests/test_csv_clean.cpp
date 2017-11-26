#include "csv_parser.h"
#include "catch.hpp"
#include <string>

using namespace csv_parser;

TEST_CASE( "Integrity Check via Statistics", "[csv_clean]" ) {
    // Header on first row
    reformat(
        "./tests/data/fake_data/ints.csv",
        "./tests/data/fake_data/ints2.csv");
    
    // 100 ints (type 2) in all columns
    CSVStat stats;
    stats.read_csv("./tests/data/fake_data/ints2.csv");
    stats.calc();

    for (int i = 0; i < 10; i++) {
        REQUIRE( stats.get_dtypes()[i][2] == 100 );
    }
}

TEST_CASE( "Test Line Skipping", "[csv_skiplines]" ) {
    reformat(
        "./tests/data/fake_data/ints_skipline.csv",
        "./tests/data/fake_data/ints_skipline2.csv",
        1
    );
    
    // 100 ints (type 2) in all columns
    CSVStat stats;
    stats.read_csv("./tests/data/fake_data/ints_skipline2.csv");
    stats.calc();

    for (int i = 0; i < 10; i++) {
        REQUIRE( stats.get_dtypes()[i][2] == 100 );
    }
}

TEST_CASE( "Converting Tab Delimited File", "[tsv_clean]" ) {
    reformat(
        "./tests/data/real_data/2016_Gaz_place_national.txt",
        "./tests/data/real_data/2016_Gaz_place_national.csv"
    );
    
    // Calculate some statistics on the cleaned CSV to verify it's good
    CSVStat stats(",", "\"", 0);
    stats.read_csv("./tests/data/real_data/2016_Gaz_place_national.csv");
    stats.calc();
    
    // 10 = INTPTLAT; 11 = INTPTLONG
    REQUIRE(ceil(stats.get_mean()[10]) == 39);
    REQUIRE(ceil(stats.get_mean()[10]) == 39);
}

TEST_CASE( "CSV Merge", "[csv_merge]") {
    merge("StormEvents.csv",
        {"./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2014_c20170718.csv",
        "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2015_c20170718.csv",
        "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2016_c20170816.csv",
        "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2017_c20170816.csv"});
}