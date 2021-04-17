#include "catch.hpp"
#include "csv.hpp"
using namespace csv;

const std::string PERSONS_CSV = "./tests/data/mimesis_data/persons.csv";

TEST_CASE("Calculating Statistics from Direct Input", "[read_csv_stat_direct]" ) {
    std::string int_str;
    std::stringstream int_list;
    for (int i = 1; i < 101; i++) {
        int_str = std::to_string(i);
        int_list << int_str << "," << int_str << "," << int_str << "\r\n";
    }

    // Expected results
    CSVFormat format;
    format.column_names({ "A", "B", "C" });

    CSVStat reader(int_list, format);

    std::vector<long double> means = { 50.5, 50.5, 50.5 };
    std::vector<long double> mins = { 1, 1, 1 };
    std::vector<long double> maxes = { 100, 100, 100 };

    REQUIRE( reader.get_mins() == mins );
    REQUIRE( reader.get_maxes() == maxes );
    REQUIRE( reader.get_mean() == means );
    REQUIRE( ceill(reader.get_variance()[0]) == 842 );

    // Make sure all integers between 1 and 100 have a count of 1
    for (int i = 1; i < 101; i++)
        REQUIRE( reader.get_counts()[0][std::to_string(i)] == 1 );

    // Confirm column at pos 0 has 100 integers (type 2)
    REQUIRE( reader.get_dtypes()[0][DataType::CSV_INT8] == 100 );
}

TEST_CASE( "Statistics - Rows of Integers", "[read_csv_stat]" ) {
    // Header on first row
    auto file = GENERATE(as<std::string> {},
        "./tests/data/fake_data/ints.csv",
        "./tests/data/fake_data/ints_newline_sep.csv"
    );

    SECTION("Compute Statistics") {
        CSVStat reader(file);

        // Expected Results
        std::vector<long double> means = {
            50.5, 50.5, 50.5, 50.5, 50.5,
            50.5, 50.5, 50.5, 50.5, 50.5
        };

        REQUIRE(reader.get_mean() == means);
        REQUIRE(reader.get_mins()[0] == 1);
        REQUIRE(reader.get_maxes()[0] == 100);
        REQUIRE(ceill(reader.get_variance()[0]) == 842);
    }
}

TEST_CASE( "Statistics - persons.csv", "[test_stat_person]" ) {
    CSVStat reader(PERSONS_CSV);
    REQUIRE(reader.get_maxes()[0] == 49999);
    REQUIRE( ceill(reader.get_mean()[2]) == 42 );
}

TEST_CASE("Data Types - persons.csv", "test_dtypes_person]") {
    auto dtypes = csv_data_types(PERSONS_CSV);

    REQUIRE(dtypes["Full Name"] == DataType::CSV_STRING);
    REQUIRE(dtypes["Age"] == DataType::CSV_INT8);
    REQUIRE(dtypes["Occupation"] == DataType::CSV_STRING);
    REQUIRE(dtypes["Email"] == DataType::CSV_STRING);
    REQUIRE(dtypes["Telephone"] == DataType::CSV_STRING);
    REQUIRE(dtypes["Nationality"] == DataType::CSV_STRING);
}
