#include "csv.hpp"
#include <catch2/catch_all.hpp>
#include <cmath>
#include <iostream>

TEST_CASE("Test delim from file", "[test_csv_reader_get_format_get_delim_from_file]") {
    csv::CSVReader reader("./tests/data/fake_data/delimeter.csv");
    char delim = reader.get_format().get_delim();
    REQUIRE(delim == ';');
}

TEST_CASE("Test delim from string", "[test_csv_reader_get_format_get_delim_from_string]") {
    std::ifstream file_stream("./tests/data/fake_data/delimeter.csv");
    std::string csv_data((std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>());
    std::stringstream ss(csv_data);

    csv::CSVReader reader(ss);
    char delim = reader.get_format().get_delim();
    REQUIRE(delim == ';');
}
