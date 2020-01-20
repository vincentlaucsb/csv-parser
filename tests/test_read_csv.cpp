/** @file
 *  Tests for CSV parsing
 */

#include <stdio.h> // remove()
#include <sstream>
#include "catch.hpp"
#include "csv.hpp"

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
    CSVGuessResult format = guess_format(
        "./tests/data/real_data/2009PowerStatus.txt");
    REQUIRE(format.delim == '|');
    REQUIRE(format.header_row == 0);
}

TEST_CASE("guess_delim() Test - Semi-Colon", "[test_guess_scolon]") {
    CSVGuessResult format = guess_format(
        "./tests/data/real_data/YEAR07_CBSA_NAC3.txt");
    REQUIRE(format.delim == ';');
    REQUIRE(format.header_row == 0);
}

TEST_CASE("guess_delim() Test - CSV with Comments", "[test_guess_comment]") {
    CSVGuessResult format = guess_format(
        "./tests/data/fake_data/ints_comments.csv");
    REQUIRE(format.delim == ',');
    REQUIRE(format.header_row == 5);
}

TEST_CASE("Prevent Column Names From Being Overwritten", "[csv_col_names_overwrite]") {
    std::vector<std::string> column_names = { "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10" };
    
    // Test against a variety of different CSVFormat objects
    std::vector<CSVFormat> formats = {};
    formats.push_back(CSVFormat::guess_csv());
    formats.push_back(CSVFormat());
    formats.back().delimiter(std::vector<char>({ ',', '\t', '|'}));
    formats.push_back(CSVFormat());
    formats.back().delimiter(std::vector<char>({ ',', '~'}));

    for (auto& format_in : formats) {
        // Set up the CSVReader
        format_in.column_names(column_names);
        CSVReader reader("./tests/data/fake_data/ints_comments.csv", format_in);

        // Assert that column names weren't overwritten
        CSVFormat format_out = reader.get_format();
        REQUIRE(reader.get_col_names() == column_names);
        REQUIRE(format_out.get_delim() == ',');
        REQUIRE(format_out.get_header() == 5);
    }
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
    // TODO: Actually check to see if flag is set
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
        auto should_fail = parse(csv_string, CSVFormat::rfc4180_strict());
    }
    catch (std::runtime_error& err) {
        caught_single_quote = true;
        error_message = err.what();
    }

    REQUIRE(caught_single_quote);
    REQUIRE(error_message.substr(0, 29) == "Unescaped single quote around");
}

TEST_CASE("Test Whitespace Trimming", "[read_csv_trim]") {
    auto row_str = GENERATE(as<std::string> {},
        "A,B,C\r\n" // Header row
        "123,\"234\n,345\",456\r\n",

        // Random spaces
        "A,B,C\r\n"
        "   123,\"234\n,345\",    456\r\n",

        // Random spaces + tabs
        "A,B,C\r\n"
        "\t\t   123,\"234\n,345\",    456\r\n",

        // Spaces in quote escaped field
        "A,B,C\r\n"
        "\t\t   123,\"   234\n,345  \t\",    456\r\n",

        // Spaces in one header column
        "A,B,        C\r\n"
        "123,\"234\n,345\",456\r\n",

        // Random spaces + tabs in header
        "\t A,  B\t,     C\r\n"
        "123,\"234\n,345\",456\r\n",

        // Random spaces in header + data
        "A,B,        C\r\n"
        "123,\"234\n,345\",  456\r\n"
    );

    SECTION("Parse Test") {
        CSVFormat format;
        format.header_row(0)
            .trim({ '\t', ' ' })
            .delimiter(',');

        auto row = parse(row_str, format);
        REQUIRE(vector<string>(row.front()) ==
            vector<string>({ "123", "234\n,345", "456" }));
        REQUIRE(row.front()["A"] == "123");
        REQUIRE(row.front()["B"] == "234\n,345");
        REQUIRE(row.front()["C"] == "456");
    }
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
        parse(csv_string, CSVFormat::rfc4180_strict());
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
    CSVReader reader(data_file, CSVFormat());
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
    CSVFormat format;
    format.column_names({ "A", "B" });

    std::stringstream csv_string;
    csv_string << "3.14,9999" << std::endl
        << "60,70" << std::endl
        << "," << std::endl;

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
}

TEST_CASE("Test read_row() CSVField - Power Status", "[read_row_csvf3]") {
    CSVReader reader("./tests/data/real_data/2009PowerStatus.txt");
    CSVRow row;

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
        }
    }
}

// Reported in: https://github.com/vincentlaucsb/csv-parser/issues/56
TEST_CASE("Leading Empty Field Regression", "[empty_field_regression]") {
    std::string csv_string(R"(category,subcategory,project name
,,foo-project
bar-category,,bar-project
	)");
    auto format = csv::CSVFormat();
    csv::CSVReader reader(format);
    reader.feed(csv_string);
    reader.end_feed();
    
    CSVRow first_row, second_row;
    REQUIRE(reader.read_row(first_row));
    REQUIRE(reader.read_row(second_row));

    REQUIRE(first_row["category"] == "");
    REQUIRE(first_row["subcategory"] == "");
    REQUIRE(first_row["project name"] == "foo-project");

    REQUIRE(second_row["category"] == "bar-category");
    REQUIRE(second_row["subcategory"] == "");
    REQUIRE(second_row["project name"] == "bar-project");
}

TEST_CASE("Test Parsing CSV with Dummy Column", "[read_csv_dummy]") {
    std::string csv_string(R"(A,B,C,
123,345,678,)");

    auto format = csv::CSVFormat();
    csv::CSVReader reader(format);
    reader.feed(csv_string);
    reader.end_feed();

    CSVRow first_row;

    REQUIRE(reader.get_col_names() == std::vector<std::string>({"A","B","C",""}));

    reader.read_row(first_row);
    REQUIRE(std::vector<std::string>(first_row) == std::vector<std::string>({
        "123", "345", "678", ""
    }));
}

// Reported in: https://github.com/vincentlaucsb/csv-parser/issues/67
TEST_CASE("Comments in Header Regression", "[comments_in_header_regression]") {
    std::string csv_string(R"(# some extra metadata
# some extra metadata
timestamp,distance,angle,amplitude
22857782,30000,-3141.59,0
22857786,30000,-3141.09,0
)");

    auto format = csv::CSVFormat();
    format.header_row(2);

    csv::CSVReader reader(format);
    reader.feed(csv_string);
    reader.end_feed();

    std::vector<std::string> expected = {
        "timestamp", "distance", "angle", "amplitude"
    };

    // Original issue: Leading comments appeared in column names
    REQUIRE(expected == reader.get_col_names());
}