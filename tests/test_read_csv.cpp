#include <stdio.h> // remove()
#include "catch.hpp"
#include "csv_parser.h"

using namespace csv;
using std::vector;
using std::string;

// guess_delim()
TEST_CASE("col_pos() Test", "[test_col_pos]") {
    int pos = get_col_pos(
        "./tests/data/real_data/2015_StateDepartment.csv",
        "Entity Type");
    REQUIRE(pos == 1);
}

TEST_CASE("guess_delim() Test - Pipe", "[test_guess_pipe]") {
    CSVFormat format = guess_format(
        "./tests/data/real_data/2009PowerStatus.txt");
    REQUIRE(format.delim == '|');
    REQUIRE(format.header == 0);
}

TEST_CASE("guess_delim() Test - Semi-Colon", "[test_guess_scolon]") {
    CSVFormat format = guess_format(
        "./tests/data/real_data/YEAR07_CBSA_NAC3.txt");
    REQUIRE(format.delim == ';');
    REQUIRE(format.header == 0);
}

TEST_CASE("guess_delim() Test - CSV with Comments", "[test_guess_comment]") {
    CSVFormat format = guess_format(
        "./tests/data/fake_data/ints_comments.csv");
    REQUIRE(format.delim == ',');
    REQUIRE(format.header == 5);
}

// get_file_info()
TEST_CASE("get_file_info() Test", "[test_file_info]") {
    CSVFileInfo info = get_file_info(
        "./tests/data/real_data/2009PowerStatus.txt");
        
    REQUIRE(info.delim == '|');
    REQUIRE(info.n_rows == 37960); // Can confirm with Excel
    REQUIRE(info.n_cols == 3);
    REQUIRE(info.col_names == vector<string>({"ReportDt", "Unit", "Power"}));
}

// Test Main Functions
TEST_CASE( "Test Reading CSV From Direct Input", "[read_csv_direct]" ) {
    string csv_string("A,B,C\r\n" // Header row
                      "123,234,345\r\n"
                      "1,2,3\r\n"
                      "1,2,3");

    // Feed Strings
    CSVReader reader;
    vector<string> row;
    reader.feed(csv_string);
    reader.end_feed();
    
    // Expected Results
    reader.read_row(row);
    vector<string> first_row = {"123", "234", "345"};
    REQUIRE( row == first_row );
}

TEST_CASE( "Test Escaped Comma", "[read_csv_comma]" ) {
    string csv_string = ("A,B,C\r\n" // Header row
                         "123,\"234,345\",456\r\n"
                         "1,2,3\r\n"
                         "1,2,3");

    auto rows = parse_to_string(csv_string);
    REQUIRE( rows.front() == vector<string>({"123", "234,345", "456"}));
}

TEST_CASE( "Test Escaped Newline", "[read_csv_newline]" ) {
    string csv_string = ("A,B,C\r\n" // Header row
                         "123,\"234\n,345\",456\r\n"
                         "1,2,3\r\n"
                         "1,2,3");

    auto rows = parse_to_string(csv_string, DEFAULT_CSV);
    REQUIRE( rows.front() == vector<string>({ "123", "234\n,345", "456" }) );
}

TEST_CASE( "Test Empty Field", "[read_empty_field]" ) {
    // Per RFC 1480, escaped quotes should be doubled up
    string csv_string = ("A,B,C\r\n" // Header row
                         "123,\"\",456\r\n");
    
    auto rows = parse_to_string(csv_string, DEFAULT_CSV);
    REQUIRE( rows.front() == vector<string>({ "123", "", "456" }) );
}

TEST_CASE( "Test Escaped Quote", "[read_csv_quote]" ) {
    // Per RFC 1480, escaped quotes should be doubled up
    string csv_string = (
        "A,B,C\r\n" // Header row
        "123,\"234\"\"345\",456\r\n"
        "123,\"234\"345\",456\r\n" // Unescaped single quote (not strictly valid)
    );
      
    auto rows = parse_to_string(csv_string, DEFAULT_CSV);
   
    // Expected Results: Double " is an escape for a single "
    vector<string> correct_row = {"123", "234\"345", "456"};

    // First Row
    REQUIRE( rows.front() == correct_row );

    // Second Row
    rows.pop_front();
    REQUIRE( rows.front() == correct_row );

    // Strict Mode
    bool caught_single_quote = false;
    std::string error_message("");

    try {
        auto strict_format = DEFAULT_CSV;
        strict_format.strict = true;

        auto should_fail = parse_to_string(csv_string, strict_format);
    }
    catch (std::runtime_error& err) {
        caught_single_quote = true;
        error_message = err.what();
    }

    REQUIRE(caught_single_quote);
    REQUIRE(error_message.substr(0, 29) == "Unescaped single quote around");
}

TEST_CASE("Test Bad Row Handling", "[read_csv_strict]") {
    string csv_string("A,B,C\r\n" // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "6,9\r\n" // Short row
        "1,2,3"),
        error_message = "";
    bool error_caught = false;

    try {
        parse(csv_string, DEFAULT_CSV_STRICT);
    }
    catch (std::runtime_error& err) {
        error_caught = true;
        error_message = err.what();
    }

    REQUIRE(error_caught);
    REQUIRE(error_message.substr(0, 14) == "Line too short");
}

TEST_CASE( "Test Read CSV with Header Row", "[read_csv_header]" ) {
    // Header on first row
    CSVReader reader("./tests/data/real_data/2015_StateDepartment.csv",
        {}, DEFAULT_CSV);
    vector<string> row;
    reader.read_row(row); // Populate row with first line
    
    // Expected Results
    vector<string> col_names = {
        "Year", "Entity Type", "Entity Group", "Entity Name",
        "Department / Subdivision", "Position", "Elected Official",
        "Judicial", "Other Positions", "Min Classification Salary",
        "Max Classification Salary", "Reported Base Wage", "Regular Pay",
        "Overtime Pay", "Lump-Sum Pay", "Other Pay", "Total Wages",
        "Defined Benefit Plan Contribution", "Employees Retirement Cost Covered",
        "Deferred Compensation Plan", "Health Dental Vision",
        "Total Retirement and Health Cost", "Pension Formula",
        "Entity URL", "Entity Population", "Last Updated",
        "Entity County", "Special District Activities"
    };
    
    vector<string> first_row = {
        "2015","State Department","","Administrative Law, Office of","",
        "Assistant Chief Counsel","False","False","","112044","129780",""
        ,"133020.06","0","2551.59","2434.8","138006.45","34128.65","0","0"
        ,"15273.97","49402.62","2.00% @ 55","http://www.spb.ca.gov/","",
        "08/02/2016","",""
    };
    REQUIRE( row == first_row );
    REQUIRE( reader.get_col_names() == col_names );
    
    // Skip to end
    while (reader.read_row(row));
    REQUIRE( reader.row_num == 246498 );
}

TEST_CASE( "Test CSV Subsetting", "[read_csv_subset]" ) {
    CSVReader reader("./tests/data/real_data/2015_StateDepartment.csv",
        { 0, 1, 2, 3, 4 }, DEFAULT_CSV);
    
    // Expected Results
    vector<string> row;
    vector<string> first_row = {
        "2015","State Department","","Administrative Law, Office of", "" };
    vector<string> col_names = {
        "Year","Entity Type","Entity Group","Entity Name","Department / Subdivision"
    };
    
    reader.read_row(row);
    REQUIRE( row == first_row );
    REQUIRE( reader.get_col_names() == col_names);

    // Skip to end
    while (reader.read_row(row));
    REQUIRE( reader.row_num == 246498 );
}

/**
TEST_CASE( "Test JSON Output", "[csv_to_json]") {
    const char * output = "./tests/temp/test.ndjson";

    CSVFormat format = DEFAULT_CSV;
    format.col_names = { "A", "B", "C" };
    CSVReaderPtr reader = parse("I,Like,Turtles\r\n", format);
    reader->to_json(output);
    
    // Expected Results
    std::ifstream test_file(output);
    string first_line;
    std::getline(test_file, first_line, '\n');
    REQUIRE( first_line == "{\"A\":\"I\",\"B\":\"Like\",\"C\":\"Turtles\"}" );
    test_file.close();

    REQUIRE(remove(output) == 0);
}

TEST_CASE( "Test JSON Output (Memory)", "[csv_to_json_mem]") {
    string csv_string(
        "A,B,C,D\r\n" // Header row
        "I,Like,Turtles,1\r\n"
        "I,Like,Turtles,2\r\n");
    CSVReaderPtr reader = parse(csv_string);
    vector<string> turtles = reader->to_json();
    
    // Expected Results
    REQUIRE( turtles[0] == "{\"A\":\"I\",\"B\":\"Like\",\"C\":\"Turtles\",\"D\":1}");
    REQUIRE( turtles[1] == "{\"A\":\"I\",\"B\":\"Like\",\"C\":\"Turtles\",\"D\":2}");
}

TEST_CASE( "Test JSON Escape", "[csv_to_json_escape]") {
    string csv_string(
        "A,B,C,D\r\n" // Header row
        "I,\"Like\"\"\",Turtles,1\r\n" // Quote escape test
        "I,\"Like\\\",Turtles,1\r\n"   // Backslash escape test
        "I,\"Like\r\n\",Turtles,1\r\n" // Newline escape test
        "I,\"Like\t\",Turtles,1\r\n"   // Tab escape test
        "I,\"Like/\",Turtles,1\r\n"    // Slash escape test
        );
    CSVReaderPtr reader = parse(csv_string);
    vector<string> turtles = reader->to_json();
    
    // Expected Results
    REQUIRE( turtles[0] == "{\"A\":\"I\",\"B\":\"Like\\\"\",\"C\":\"Turtles\",\"D\":1}");
    REQUIRE( turtles[1] == "{\"A\":\"I\",\"B\":\"Like\\\\\",\"C\":\"Turtles\",\"D\":1}");
    REQUIRE( turtles[2] == "{\"A\":\"I\",\"B\":\"Like\\\r\\\n\",\"C\":\"Turtles\",\"D\":1}");
    REQUIRE( turtles[3] == "{\"A\":\"I\",\"B\":\"Like\\\t\",\"C\":\"Turtles\",\"D\":1}");
    REQUIRE( turtles[4] == "{\"A\":\"I\",\"B\":\"Like\\/\",\"C\":\"Turtles\",\"D\":1}");
}
**/

// read_row()
TEST_CASE("Test read_row() CSVField - Easy", "[read_row_csvf1]") {
    // Test that integers are type-casted properly
    CSVReader reader("./tests/data/fake_data/ints.csv");
    CSVRow row;

    while (reader.read_row(row)) {
        for (size_t i = 0; i < row.size(); i++) {
            REQUIRE( row[i].get<int>() <= 100 );
            REQUIRE( row[i].is_int() );
        }
    }
}

TEST_CASE("Test read_row() CSVField - Memory", "[read_row_csvf2]") {
    CSVFormat format = DEFAULT_CSV;
    format.col_names = { "A", "B" };

    string csv_string = (
        "3.14,9999\r\n"
        "60,70\r\n"
        ",\r\n");

    auto rows = parse(csv_string, format);
    CSVRow& row = rows.front();

    // First Row
    REQUIRE((row[0].is_float() && row[0].is_num()));
    REQUIRE(row[0].get<std::string>().substr(0, 4) == "3.14");

    // Second Row
    rows.pop_front();
    row = rows.front();
    REQUIRE((row[0].is_int() && row[0].is_num()));
    REQUIRE((row[1].is_int() && row[1].is_num()));
    REQUIRE(row[0].get<std::string>() == "60");
    REQUIRE(row[1].get<std::string>() == "70");

    // Third Row
    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0].is_null());
    REQUIRE(row[1].is_null());
}

TEST_CASE("Test read_row() CSVField - Power Status", "[read_row_csvf3]") {
    CSVReader reader("./tests/data/real_data/2009PowerStatus.txt");
    CSVRow row;
    bool caught_error = false;

    size_t date = reader.index_of("ReportDt"),
        unit = reader.index_of("Unit"),
        power = reader.index_of("Power");
    
    // Try to find a non-existent column
    REQUIRE(reader.index_of("metallica") == CSV_NOT_FOUND);

    for (size_t i = 0; reader.read_row(row); i++) {
        // Assert correct types
        REQUIRE(row[date].is_str());
        REQUIRE(row[unit].is_str());
        REQUIRE(row[power].is_int());

        // Spot check
        if (i == 2) {
            REQUIRE(row[power].get<int>() == 100);
            REQUIRE(row[date].get<std::string>() == "12/31/2009");
            REQUIRE(row[unit].get<std::string>() == "Beaver Valley 1");

            // Assert misusing API throws the appropriate errors
            try {
                row[0].get<long long int>();
            }
            catch (std::runtime_error&) {
                caught_error = true;
            }

            REQUIRE(caught_error);
            caught_error = false;

            try {
                row[0].get<double>();
            }
            catch (std::runtime_error&) {
                caught_error = true;
            }

            REQUIRE(caught_error);
        }
    }
}