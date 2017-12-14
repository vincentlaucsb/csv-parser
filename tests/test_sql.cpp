#include "catch.hpp"
#include "csv_parser.h"
#include "sqlite3.h"
#include <set>

using namespace csv_parser;
using extra::csv_to_sql;
using extra::csv_join;
using helpers::path_split;
using sql::sql_sanitize;
using sql::sqlite_types;
using std::vector;
using std::string;
using std::set;

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

// Test CSV-to-SQLite Data Type Guessing
TEST_CASE("SQLite Types - Power Status", "[test_sqlite_types1]") {
    vector<string> dtypes = sqlite_types("./tests/data/real_data/2009PowerStatus.txt");
    REQUIRE(dtypes[0] == "string");
    REQUIRE(dtypes[1] == "string");
    REQUIRE(dtypes[2] == "integer");
}

TEST_CASE("SQLite Types - US Places", "[test_sqlite_types2]") {
    vector<string> dtypes = sqlite_types(
        "./tests/data/real_data/2016_Gaz_place_national.txt");
    set<size_t> int_cols = { 1, 2, 4, 6, 7 };
    set<size_t> float_cols = { 8, 9, 10, 11 };

    for (size_t i = 0; i < dtypes.size(); i++) {
        if (int_cols.find(i) != int_cols.end()) {
            REQUIRE(dtypes[i] == "integer");
        }
        else if(float_cols.find(i) != float_cols.end()) {
            REQUIRE(dtypes[i] == "float");
        }
        else {
            REQUIRE(dtypes[i] == "string");
        }
    }
}

// Test Main Functionality
TEST_CASE("Path Split", "[test_path_split]") {
    vector<string> expected = { 
        ".", "tests", "data", "fake_data", "ints.csv"
    };
    
    REQUIRE(path_split("./tests/data/fake_data/ints.csv") == expected);
}

TEST_CASE("CSV to SQL - ints.csv", "[test_to_sql_ints]") {
    csv_to_sql("./tests/data/fake_data/ints.csv", "./tests/temp/ints.sqlite");
    
    sqlite3* db_handle;
    sqlite3_stmt* get_mean;
    sqlite3_stmt* get_count;
    const char* unused;
    
    sqlite3_open("./tests/temp/ints.sqlite", &db_handle);
    
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

TEST_CASE("CSV Join - ints", "[test_join_ints]") {
    // Test join functionality
    csv_join(
        "./tests/data/fake_data/ints_squared.csv",
        "./tests/data/fake_data/ints_cubed.csv",
        "./tests/temp/ints_join.csv"
    );

    CSVReader reader("./tests/temp/ints_join.csv");
    vector<CSVField> row;
    vector<int> dtypes;
    int i = 1;

    REQUIRE(reader.get_col_names() == vector<string>({ "a", "b", "c" }));

    while (reader.read_row(row)) {
        REQUIRE(row[0].is_int() == 1);
        REQUIRE(row[0].get_int() == i);
        REQUIRE(row[1].get_int() == pow(i,2));
        REQUIRE(row[2].get_int() == pow(i,3));
        i++;
    }
}