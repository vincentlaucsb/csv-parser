#include <catch2/catch_all.hpp>
#include "csv.hpp"

#include <cstdint>
#include <limits>
#include <string>

using namespace csv;
using namespace csv::internals;

TEST_CASE("data_type() exposes csv-parser scalar classification policy", "[data_type]") {
    //! [Scalar Classification Policy]
    REQUIRE(data_type(csv::string_view()) == DataType::CSV_NULL);
    REQUIRE(data_type("") == DataType::CSV_NULL);
    REQUIRE(data_type("not-a-number") == DataType::CSV_STRING);
    REQUIRE(data_type("510-123-4567") == DataType::CSV_STRING);

    REQUIRE(data_type("127") == DataType::CSV_INT8);
    REQUIRE(data_type("128") == DataType::CSV_INT16);
    REQUIRE(data_type("32768") == DataType::CSV_INT32);
    REQUIRE(data_type("2147483648") == DataType::CSV_INT64);

    std::string too_big = std::to_string((std::numeric_limits<std::int64_t>::max)());
    too_big.push_back('1');
    REQUIRE(data_type(too_big) == DataType::CSV_BIGINT);

    REQUIRE(data_type("0x10") == DataType::CSV_INT8);
    REQUIRE(data_type("3.14") == DataType::CSV_DOUBLE);
    REQUIRE(data_type("1E-06") == DataType::CSV_DOUBLE);
    //! [Scalar Classification Policy]
}

TEST_CASE("data_type() recognizes scalar types without materializing values", "[data_type][test_csv_field_bool]") {
    //! [Bool and Timestamp Classification]
    REQUIRE(data_type("true") == DataType::CSV_BOOL);
    REQUIRE(data_type("false") == DataType::CSV_BOOL);
    REQUIRE(data_type("2024-01-31T23:59:58Z") == DataType::CSV_TIMESTAMP);

    CSVField true_field("true");
    CSVField false_field("false");
    CSVField timestamp_field("2024-01-31T23:59:58Z");

    REQUIRE(true_field.type() == DataType::CSV_BOOL);
    REQUIRE(false_field.type() == DataType::CSV_BOOL);
    REQUIRE(timestamp_field.type() == DataType::CSV_TIMESTAMP);

    REQUIRE(true_field.get<bool>());
    REQUIRE_FALSE(false_field.get<bool>());
    //! [Bool and Timestamp Classification]
}

TEST_CASE("CSVField materializes classified numeric values", "[data_type][test_csv_field]") {
    //! [Materialized Numeric Values]
    REQUIRE(CSVField("0x10").get<long long>() == 16);
    REQUIRE(CSVField("-69").get<long long>() == -69);
    REQUIRE(CSVField("2018").get<long long>() == 2018);
    REQUIRE(internals::is_equal(CSVField("0.15").get<long double>(), 0.15L));
    REQUIRE(internals::is_equal(CSVField("-1.5E3").get<long double>(), -1500.0L));
    //! [Materialized Numeric Values]
}

TEST_CASE("CSVField supports scientific notation floats", "[data_type][scientific_notation]") {
    //! [Scientific Notation Floats]
    REQUIRE(data_type("1E-06") == DataType::CSV_DOUBLE);
    REQUIRE(internals::is_equal(CSVField("1E-06").get<long double>(), 0.000001L));
    REQUIRE(internals::is_equal(CSVField("-1.5E3").get<long double>(), -1500.0L));
    REQUIRE(internals::is_equal(CSVField("+1.5e+003").get<long double>(), 1500.0L));

    REQUIRE(data_type("1E -06") == DataType::CSV_STRING);
    REQUIRE(data_type("1.5e") == DataType::CSV_STRING);
    //! [Scientific Notation Floats]
}
