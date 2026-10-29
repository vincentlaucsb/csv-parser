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

#if CSV_EXCEPTIONS_ENABLED
static_assert(std::is_member_function_pointer<decltype(&csv::CSVRow::to_json)>::value,
    "Preserve the single JSON overload used by bindings and pointer-to-member consumers.");
static_assert(std::is_member_function_pointer<decltype(&csv::DataFrameRow<std::string>::to_json)>::value,
    "DataFrame row JSON must preserve its original enabled signature too.");
#endif

#ifdef CSV_TEST_EXPECT_EXPECTED
#ifndef CSV_HAS_STD_EXPECTED
#error "The C++23 consumer must exercise std::expected conversions."
#endif
#endif

template<typename T>
auto has_const_decimal_parse(int) -> decltype(
    std::declval<const T&>().try_parse_decimal(std::declval<long double&>()), std::true_type());
template<typename>
std::false_type has_const_decimal_parse(...);
static_assert(!decltype(has_const_decimal_parse<csv::CSVField>(0))::value,
    "Changing decimal interpretation must require a mutable field.");
static_assert(std::is_same<decltype(std::declval<const csv::CSVField&>().is_num()), bool>::value,
    "Classification is logically const.");
#ifdef CSV_HAS_CXX17
constexpr csv::CSVField constant_text("text");
static_assert(constant_text.get_sv().size() == 4, "Text views retain constexpr support.");
#endif

// Keep these checks in the exact-standard consumer, including genuine C++11.
static_assert(std::is_same<std::iterator_traits<csv::CSVRow::iterator>::reference,
    csv::CSVField>::value, "Field iteration returns values, not dangling references.");
static_assert(std::is_same<std::iterator_traits<csv::CSVRow::iterator>::iterator_category,
    std::input_iterator_tag>::value, "Legacy forward iterators require actual references.");
#ifdef CSV_HAS_CXX20
static_assert(std::random_access_iterator<csv::CSVRow::iterator>);
static_assert(std::random_access_iterator<csv::CSVRow::reverse_iterator>);
#endif

int csv_error_second_tu();
int csv_error_storage_test();
int csv_row_iterator_allocation_test();

int main() {
    if (const int failure = csv_error_storage_test()) return failure;
    if (const int failure = csv_row_iterator_allocation_test()) return failure;
    csv::CSVError error;
    std::istringstream input("id,name,value\n1,Alice,10\n2,Bob,20\n");
    auto reader = csv::CSVReader::try_open(input, error);
    if (!reader || error) return 1;
    // Separate input leaves the DataFrame fixture untouched. Iterator navigation
    // survives source-row reuse; field wrappers retain independent lazy caches.
    {
        auto fields = csv::try_parse("a,b,c\n123,234,345\n", error);
        if (!fields || error) return 20;
        csv::CSVRow csv_row;
        if (!fields->try_read_row(csv_row, error) || error) return 21;
        auto first = csv_row.begin();
        auto reverse = csv_row.rbegin();
        csv_row = csv::CSVRow();
        fields.reset();
        const auto saved = *first;
        if (saved.get_sv() != "123" || (++first)->get_sv() != "234"
            || (1 + first)->get_sv() != "345" || first[-1].get_sv() != "123") return 22;
        if (reverse->get_sv() != "345" || reverse[1].get_sv() != "234"
            || (++reverse)->get_sv() != "234") return 23;
        int parsed = 0;
        if (!saved.is_num() || !saved.try_get(parsed) || parsed != 123) return 24;
#if CSV_EXCEPTIONS_ENABLED
        if (saved.get<int>() != 123) return 26;
#endif
#ifdef CSV_HAS_CXX20
        auto standard = std::make_reverse_iterator(reverse.base());
        if ((*standard).get_sv() != "234" || standard->get_sv() != "234") return 25;
#endif
    }
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
    const csv::CSVField expected_number("12");
    auto number = expected_number.as<int>();
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
