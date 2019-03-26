/** @file
 *  Tests for CSV parsing
 */

#include <stdio.h> // remove()
#include "catch.hpp"
#include "csv_parser.hpp"

using namespace csv;
using std::vector;
using std::string;

//
// guess_delim()
//
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
    auto rows = "A,B,C\r\n" // Header row
                "123,234,345\r\n"
                "1,2,3\r\n"
                "1,2,3"_csv;
   
    // Expected Results
    auto row = rows.front();
    vector<string> first_row = {"123", "234", "345"};
    REQUIRE( vector<string>(row) == first_row );
}

TEST_CASE("Assert UTF-8 Handling Works", "[read_utf8_direct]") {
    auto rows = "\uFEFFA,B,C\r\n" // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3"_csv;

    // Expected Results
    auto row = rows.front();
    vector<string> first_row = { "123", "234", "345" };
    REQUIRE(vector<string>(row) == first_row);
}

//! [Escaped Comma]
TEST_CASE( "Test Escaped Comma", "[read_csv_comma]" ) {
    auto rows = "A,B,C\r\n" // Header row
                "123,\"234,345\",456\r\n"
                "1,2,3\r\n"
                "1,2,3"_csv;

    REQUIRE( vector<string>(rows.front()) == 
        vector<string>({"123", "234,345", "456"}));
}
//! [Escaped Comma]

TEST_CASE( "Test Escaped Newline", "[read_csv_newline]" ) {
    auto rows = "A,B,C\r\n" // Header row
                "123,\"234\n,345\",456\r\n"
                "1,2,3\r\n"
                "1,2,3"_csv;

    REQUIRE( vector<string>(rows.front()) == 
        vector<string>({ "123", "234\n,345", "456" }) );
}

TEST_CASE( "Test Empty Field", "[read_empty_field]" ) {
    // Per RFC 1480, escaped quotes should be doubled up
    auto rows = "A,B,C\r\n" // Header row
                "123,\"\",456\r\n"_csv;

    REQUIRE( vector<string>(rows.front()) == 
        vector<string>({ "123", "", "456" }) );
}

//! [Parse Example]
TEST_CASE( "Test Escaped Quote", "[read_csv_quote]" ) {
    // Per RFC 1480, escaped quotes should be doubled up
    string csv_string = (
        "A,B,C\r\n" // Header row
        "123,\"234\"\"345\",456\r\n"
        "123,\"234\"345\",456\r\n" // Unescaped single quote (not strictly valid)
    );
      
    auto rows = parse(csv_string);
   
    // Expected Results: Double " is an escape for a single "
    vector<string> correct_row = {"123", "234\"345", "456"};

    // First Row
    REQUIRE( vector<string>(rows.front()) == correct_row );

    // Second Row
    rows.pop_front();
    REQUIRE( vector<string>(rows.front()) == correct_row );

//! [Parse Example]

    // Strict Mode
    bool caught_single_quote = false;
    std::string error_message("");

    try {
        auto strict_format = DEFAULT_CSV;
        strict_format.strict = true;

        auto should_fail = parse(csv_string, strict_format);
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

TEST_CASE("Non-Existent CSV", "[read_ghost_csv]") {
    // Make sure attempting to parse a non-existent CSV throws an error
    bool error_caught = false;

    try {
        CSVReader reader("./lochness.csv");
    }
    catch (std::runtime_error& err) {
        error_caught = true;
        REQUIRE(err.what() == std::string("Cannot open file ./lochness.csv"));
    }

    REQUIRE(error_caught);
}

TEST_CASE( "Test Read CSV with Header Row", "[read_csv_header]" ) {
    // Header on first row
    const std::string data_file = "./tests/data/real_data/2015_StateDepartment.csv";
    CSVReader reader(data_file, DEFAULT_CSV);
    CSVRow row;
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

    REQUIRE( vector<string>(row) == first_row );
    REQUIRE( get_col_names(data_file) == col_names );
    
    // Skip to end
    while (reader.read_row(row));
    REQUIRE( reader.row_num == 246498 );
}

//
// read_row()
//
//! [CSVField Example]
TEST_CASE("Test read_row() CSVField - Easy", "[read_row_csvf1]") {
    // Test that integers are type-casted properly
    CSVReader reader("./tests/data/fake_data/ints.csv");
    CSVRow row;

    while (reader.read_row(row)) {
        for (size_t i = 0; i < row.size(); i++) {
            REQUIRE(row[i].is_int());
            REQUIRE(row[i].get<int>() <= 100);
        }
    }
}
//! [CSVField Example]

TEST_CASE("Test read_row() CSVField - Memory", "[read_row_csvf2]") {
    CSVFormat format = DEFAULT_CSV;
    format.col_names = { "A", "B" };

    std::stringstream csv_string;
    double big_num = ((double)std::numeric_limits<long long>::max() * 2.0);

    csv_string << "3.14,9999" << std::endl
        << "60,70" << std::endl
        << "," << std::endl
        << (std::numeric_limits<long>::max() - 100) << "," 
            << (std::numeric_limits<long long>::max()/2) << std::endl
        << std::to_string(big_num) << "," << std::endl;

    auto rows = parse(csv_string.str(), format);
    CSVRow row = rows.front();

    // First Row
    REQUIRE((row[0].is_float() && row[0].is_num()));
    REQUIRE(row[0].get<std::string>().substr(0, 4) == "3.14");
    REQUIRE(internals::is_equal(row[0].get<double>(), 3.14));

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

    // Fourth Row
    rows.pop_front();
    row = rows.front();
    
    // Older versions of g++ have issues with numeric_limits
#if (!defined(__GNUC__) || __GNUC__ >= 5)
    REQUIRE((row[0].type() == CSV_INT || row[0].type() == CSV_LONG_INT));
    REQUIRE(row[0].get<long>() == std::numeric_limits<long>::max() - 100);
    // REQUIRE(row[1].get<long long>() == std::numeric_limits<long long>::max()/2);
#endif

    // Fourth Row
    rows.pop_front();
    row = rows.front();
    double big_num_csv = row[0].get<double>();
    REQUIRE(row[0].type() == CSV_DOUBLE); // Overflow
    REQUIRE(internals::is_equal(big_num_csv, big_num));
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
            REQUIRE(row[date].get<>() == "12/31/2009"); // string_view
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
            catch (std::runtime_error& err) {
                REQUIRE(err.what() == std::string("Attempted to convert a "
                    "value of type string to double."));
                caught_error = true;
            }

            REQUIRE(caught_error);
        }
    }
}