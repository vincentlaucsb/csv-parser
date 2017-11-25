#include "catch.hpp"
#include "csv_parser.h"
#include "sqlite3.h"

using namespace csv_parser;
using std::vector;
using std::string;

// Test Helpers
TEST_CASE("SQL Sanitize", "[test_sql_sanitize]") {
    vector<string> bad_boys = {
        "bad.name", "0badname", "123bad\\name", "bad,name"
    };
    
    vector<string> expected = {
        "badname", "_0badname", "_123badname", "badname"
    };
    
    REQUIRE(sql_sanitize(bad_boys) == expected);
}

// Test Main Functionality
TEST_CASE("Path Split", "[test_path_split]") {
    vector<string> expected = { 
        ".", "tests", "data", "fake_data", "ints.csv"
    };
    
    REQUIRE(path_split("./tests/data/fake_data/ints.csv") == expected);
}

TEST_CASE("CSV to SQL", "[test_csv_to_sql]") {
    csv_to_sql("./tests/data/fake_data/ints.csv", "ints.sqlite");
    
    sqlite3* db_handle;
    sqlite3_stmt* get_mean;
    sqlite3_stmt* get_count;
    const char* unused;
    char* error_message;
    
    sqlite3_open("ints.sqlite", &db_handle);
    
    // TO DO: Write a more robust test of this
    sqlite3_prepare_v2(db_handle, "SELECT A FROM ints WHERE A='50';", -1, &get_mean, &unused);
    sqlite3_step(get_mean);
    
    sqlite3_prepare_v2(db_handle, "SELECT count(*) FROM ints;", -1, &get_count, &unused);
    sqlite3_step(get_count);

    // Test
    REQUIRE(std::string((char *)sqlite3_column_text(get_mean, 0)) == "50");
    REQUIRE(sqlite3_column_int(get_count, 0) == 100);
    
    // Clean Up
    sqlite3_finalize(get_mean);
    sqlite3_close(db_handle);
}