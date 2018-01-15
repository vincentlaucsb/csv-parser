#include <stdio.h> // For remove()
#include "catch.hpp"
#include "csv_parser.h"

using namespace csv;

TEST_CASE("CSV Comma Escape", "[test_csv_comma]") {
    std::string input = "Furthermore, this should be quoted.";
    std::string correct = "\"Furthermore, this should be quoted.\"";

    REQUIRE(csv_escape(input) == correct);
}

TEST_CASE("CSV Quote Escape", "[test_csv_quote]") {
    std::string input = "\"What does it mean to be RFC 4180 compliant?\" she asked.";
    std::string correct = "\"\"\"What does it mean to be RFC 4180 compliant?\"\" she asked.\"";

    REQUIRE(csv_escape(input) == correct);
}

TEST_CASE("CSV Quote Minimal", "[test_csv_quote_min]") {
    std::string input = "This should not be quoted";
    REQUIRE(csv_escape(input) == input);
}

TEST_CASE("CSV Quote All", "[test_csv_quote_all]") {
    std::string input = "This should be quoted";
    std::string correct = "\"This should be quoted\"";
    REQUIRE(csv_escape(input, false) == correct);
}

TEST_CASE("Integrity Check via Statistics", "[csv_clean]") {
    // Header on first row
    const char * output = "./tests/temp/ints2.csv";
    reformat("./tests/data/fake_data/ints.csv", output);

    // 100 ints (type 2) in all columns
    CSVStat stats(output);
    for (int i = 0; i < 10; i++)
        REQUIRE(stats.get_dtypes()[i][2] == 100);

    // Clean-up
    stats.close();
    REQUIRE(remove(output) == 0);
}

TEST_CASE("Test Line Skipping", "[csv_skiplines]") {
    const char * output = "./tests/temp/ints_skipline2.csv";
    reformat("./tests/data/fake_data/ints_skipline.csv", output, 1);

    // 100 ints (type 2) in all columns
    CSVStat stats(output);
    for (int i = 0; i < 10; i++)
        REQUIRE(stats.get_dtypes()[i][2] == 100);
    
    // Clean-up
    stats.close();
    REQUIRE(remove(output) == 0);
}

TEST_CASE("Converting Tab Delimited File", "[tsv_clean]") {
    const char * output = "./tests/temp/2016_Gaz_place_national.csv";
    reformat("./tests/data/real_data/2016_Gaz_place_national.txt", output);

    // Calculate some statistics on the cleaned CSV to verify it's good
    // 10 = INTPTLAT; 11 = INTPTLONG
    CSVStat stats(output);
    REQUIRE(ceil(stats.get_mean()[10]) == 39);
    REQUIRE(ceil(stats.get_mean()[10]) == 39);

    // Clean-up
    stats.close();
    REQUIRE(remove(output) == 0);
}

TEST_CASE("CSV Merge", "[csv_merge]") {
    merge("./tests/temp/StormEvents.csv",
    { "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2014_c20170718.csv",
        "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2015_c20170718.csv",
        "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2016_c20170816.csv",
        "./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2017_c20170816.csv" });

    // Clean-up
    REQUIRE(remove("./tests/temp/StormEvents.csv") == 0);
}