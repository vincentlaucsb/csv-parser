#include "csv.hpp"
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

template<typename T>
auto has_throwing_get(int) -> decltype(
    std::declval<T&>().template get<int>(), std::true_type());
template<typename>
std::false_type has_throwing_get(...);
template<typename T>
auto has_quote_char(int) -> decltype(
    std::declval<T&>().quote(char{}), std::true_type());
template<typename>
std::false_type has_quote_char(...);
template<typename T>
auto has_begin(int) -> decltype(std::declval<T&>().begin(), std::true_type());
template<typename>
std::false_type has_begin(...);

static_assert(CSV_EXCEPTIONS_ENABLED == CSV_TEST_EXCEPTIONS,
    "The consumer must use the requested compiler exception mode.");
#if CSV_TEST_CXX_STANDARD == 11 && !defined(_MSC_VER)
static_assert(CSV_CPLUSPLUS == 201103L,
    "The C++11 consumer must not be upgraded by a test framework dependency.");
#endif
static_assert(decltype(has_throwing_get<csv::CSVField>(0))::value
    == static_cast<bool>(CSV_EXCEPTIONS_ENABLED),
    "Throwing conversions must be unavailable when exceptions are disabled.");
static_assert(decltype(has_quote_char<csv::CSVFormat>(0))::value
    == static_cast<bool>(CSV_EXCEPTIONS_ENABLED),
    "A disabled quote(char) must not silently select quote(bool).");
static_assert(std::is_constructible<csv::CSVReader, csv::string_view>::value
    == static_cast<bool>(CSV_EXCEPTIONS_ENABLED),
    "Fallible reader construction must use a factory without exceptions.");
static_assert(std::is_convertible<csv::DataFrame<>::row_type,
    std::vector<std::string>>::value == static_cast<bool>(CSV_EXCEPTIONS_ENABLED),
    "Fallible row materialization must use a checked API without exceptions.");
static_assert(decltype(has_begin<csv::DataFrame<>::column_type>(0))::value
    == static_cast<bool>(CSV_EXCEPTIONS_ENABLED),
    "Column iteration cannot hide short-row failures without exceptions.");
static_assert(decltype(has_begin<csv::DataFrame<>>(0))::value,
    "Materialized DataFrame row iteration must remain available.");

#ifdef CSV_TEST_EXPECT_EXPECTED
#ifndef CSV_HAS_STD_EXPECTED
#error "The C++23 consumer must exercise std::expected conversions."
#endif
#endif

int csv_error_second_tu();

int main() {
    csv::CSVError error;
    std::istringstream input("id,name,value\n1,Alice,10\n2,Bob,20\n");
    auto reader = csv::CSVReader::try_open(input, error);
    if (!reader || error) return 1;
    auto frame = csv::DataFrame<>::try_create(*reader, error);
    if (!frame || error || frame->size() != 2) return 2;
    auto row = frame->try_at(1, error);
    if (error) return 3;
    auto cell = row.try_field("value", error);
    if (error) return 4;
    int value = 0;
    csv::CSVConversionError conversion;
    if (!cell.try_get(value, conversion) || value != 20) return 5;
    if (conversion != csv::CSVConversionError::None) return 6;

    std::ostringstream output;
    csv::CSVWriter<std::ostringstream> writer(output);
    if (!writer.try_write_row(error, std::vector<std::string>{"A", "B"})) return 7;
    if (!writer.try_flush(error) || error || output.str() != "A,B\n") return 8;
#ifdef CSV_HAS_STD_EXPECTED
    auto number = csv::CSVField("12").as<int>();
    auto invalid = csv::CSVField("bad").as<int>();
    if (!number || *number != 12 || invalid
        || invalid.error() != csv::CSVConversionError::NotANumber) return 9;
    auto text = csv::CSVField("text").as<std::string>();
    auto view = csv::CSVField("view").as<csv::string_view>();
    auto wide = csv::CSVField("2.5").as<long double>();
    if (!text || *text != "text" || !view || *view != "view"
        || !wide || *wide != 2.5L) return 14;
    std::istringstream expected_input("a,b\n5,9\n");
    auto expected_reader = csv::CSVReader::try_open(expected_input);
    if (!expected_reader || !*expected_reader) return 15;
    auto expected_failure = csv::CSVReader::try_open(
        "tests/data/fake_data/explicit_errors_missing_directory/file.csv");
    if (expected_failure
        || expected_failure.error().code != csv::CSVErrorCode::OpenFailed) return 16;
#endif
    return csv_error_second_tu();
}
