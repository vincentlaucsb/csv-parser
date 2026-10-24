#include "csv.hpp"
#include <sstream>

// A second consumer catches ODR errors in generated headers and keeps the
// compatibility check independent of Catch2's C++14 requirement.
int csv_error_second_tu() {
    csv::CSVError error;
    std::istringstream input("left,right\n3,7\n");
    auto reader = csv::CSVReader::try_open(input, error);
    if (!reader || error) return 10;
    csv::CSVRow row;
    if (!reader->try_read_row(row, error) || error) return 11;
    auto field = row.try_field("right", error);
    int value = 0;
    if (error || !field.try_get(value) || value != 7) return 12;
    if (reader->try_read_row(row, error) || error) return 13;
    return 0;
}
