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

TEST_CASE("CSV to SQL - ints.csv", "[test_to_sql_ints]") {
    csv_to_sql("./tests/data/fake_data/ints.csv", "ints.sqlite");
    
    sqlite3* db_handle;
    sqlite3_stmt* get_mean;
    sqlite3_stmt* get_count;
    const char* unused;
    
    sqlite3_open("ints.sqlite", &db_handle);
    
    // Assert Correct Number of Entries
    sqlite3_prepare_v2(db_handle, "SELECT count(*) FROM ints;", -1, &get_count, &unused);
    sqlite3_step(get_count);
    REQUIRE(sqlite3_column_int(get_count, 0) == 100);

    // Assert Correct Mean
    vector<string> col_names = { "a", "b", "c", "d", "e", "f", "g", "h", "i", "j" };

    for (auto it = col_names.begin(); it != col_names.end(); ++it) {
        sqlite3_prepare_v2(db_handle,
            (const char *)("SELECT avg(" + *it + ") FROM ints").c_str(),
            -1, &get_mean, &unused);
        sqlite3_step(get_mean);
        REQUIRE(sqlite3_column_double(get_mean, 0) == 50.5);
    }
    
    // Clean Up
    sqlite3_finalize(get_mean);
    sqlite3_finalize(get_count);
    sqlite3_close(db_handle);
}