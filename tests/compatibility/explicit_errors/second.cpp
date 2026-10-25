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
    // Exercise the out-of-line classifier/type-inference path in the exact
    // consumer standard, independently of Catch2's C++14 minimum.
    std::istringstream typed("left,right\n3,7\n");
    auto types = csv::try_csv_data_types(typed, error);
    if (error || types.size() != 2
        || types["left"] != csv::DataType::CSV_INT8) return 17;
    std::istringstream short_input("left,right\n3\n");
    csv::CSVFormat format;
    format.header_row(0).variable_columns(csv::VariableColumnPolicy::KEEP);
    auto short_reader = csv::CSVReader::try_open(short_input, error, format);
    if (!short_reader || error) return 18;
    types = csv::try_csv_data_types(*short_reader, error);
    if (!types.empty() || error.code != csv::CSVErrorCode::IndexOutOfBounds) return 19;
    return 0;
}
