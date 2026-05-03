@page scalar_conversions Scalar Conversion Reference

# Scalar Conversion Reference

CSVField conversions use the same scalar classification policy as
[classify_scalar](https://github.com/vincentlaucsb/classify_scalar), a
separately maintained scalar classification library with its own test suite,
configurable scalar grammars, and benchmarking notes.

Use classify_scalar directly if you want csv-parser's scalar behavior outside
CSV parsing, or if you want to define a related classification policy for
another data format.

## Conversion APIs

| API | Result | Failure |
| --- | --- | --- |
| \link csv::CSVField::get() `field.get<T>()` \endlink | Returns `T` | Throws `std::runtime_error` |
| \link csv::CSVField::try_get() `field.try_get<T>(out)` \endlink | Assigns `out`, returns `true` | Returns `false` and leaves `out` unchanged unless documented otherwise |
| \ref CSVField_optional_conversion "`std::optional<T> value = field`" | Produces `std::optional<T>` | Produces `std::nullopt` |
| \link csv::CSVField::as() `field.as<T>()` \endlink | Produces `std::expected<T, CSVConversionError>` | Produces `std::unexpected(CSVConversionError)` |
| \link csv::CSVField::try_parse_hex() `field.try_parse_hex<T>(out)` \endlink | Assigns an integral `T`, returns `true` | Returns `false` |
| \link csv::CSVField::try_parse_decimal() `field.try_parse_decimal(out, decimal_symbol)` \endlink | Assigns `long double`, returns `true` | Returns `false` |
| \link csv::CSVField::try_parse_timestamp() `field.try_parse_timestamp<T>(out)` \endlink | Assigns a Unix timestamp, duration, or time point, returns `true` | Returns `false` |

`std::optional` conversions require C++17. `std::expected` conversions require
C++23 and a standard library that provides `std::expected`.
csv::CSVField::try_parse_timestamp() for `uint64_t` and `std::chrono` timestamp
targets is available in all supported C++ versions.

csv::CSVField::as() reports conversion failures with csv::CSVConversionError.
The enum values are:

| CSVConversionError | Meaning |
| --- | --- |
| `None` | Conversion succeeded. |
| `NotANumber` | The field is not compatible with the requested target type. |
| `Overflow` | The parsed value does not fit in the requested target type. |
| `FloatToInt` | A floating point field was requested as an integral type. |
| `NegativeToUnsigned` | A negative value was requested as an unsigned type. |

Use csv::csv_conversion_error_message() to convert a CSVConversionError to a
stable human-readable message.

## Classification Policy

`data_type()` exposes csv-parser's scalar classification policy. It recognizes
empty values, strings, signed integer widths, big integers, hexadecimal integers,
floating point values, booleans, and timestamps.

| Classified value | DataType result | Notes |
| --- | --- | --- |
| Empty field | `CSV_NULL` | Empty `csv::string_view` values are treated as null fields. |
| Non-scalar text | `CSV_STRING` | Strings such as phone numbers stay strings instead of being partly parsed. |
| Integer | `CSV_INT8`, `CSV_INT16`, `CSV_INT32`, or `CSV_INT64` | The smallest signed width that can hold the value is used. |
| Integer outside `int64_t` | `CSV_BIGINT` | The value remains numeric for schema inference but is not narrowed into `long long`. |
| Hex integer | Integer DataType | `data_type()` and csv::CSVField::get() require the `0x` prefix for hex classification. |
| Floating point | `CSV_DOUBLE` | Includes scientific notation. |
| Boolean | `CSV_BOOL` | `true` and `false`, case-insensitive. |
| Timestamp | `CSV_TIMESTAMP` | ISO 8601-style date/time strings. |

\snippet tests/test_data_type.cpp Scalar Classification Policy
\snippet tests/test_data_type.cpp Bool and Timestamp Classification

## Integers and Hex

Integral conversions preserve range checks. Overflow, float-to-int conversion,
and negative-to-unsigned conversion are rejected instead of relying on C++'s
native cast behavior.

csv::CSVField::get() accepts hexadecimal integers when the field uses the `0x`
prefix. csv::CSVField::try_parse_hex() accepts hexadecimal values with or
without the prefix and rejects values outside the target type's range.

Both paths use classify_scalar's built-in ASCII whitespace trimming before
classification or parsing.

\snippet tests/test_data_type.cpp Materialized Numeric Values
\snippet tests/test_csv_field.cpp CSVField Hex Conversion

## Floats

Floating point conversions support decimal values and scientific notation.
Converting a floating point field to an integral type is rejected. Loss of
floating point precision is not currently checked.

\snippet tests/test_csv_field.cpp CSVField Floating Point Conversion

### Scientific notation

Scientific notation is classified as `CSV_DOUBLE` and can be materialized through
csv::CSVField::get() or csv::CSVField::try_get() with a floating point target.
Malformed scientific notation is classified as `CSV_STRING`.

Supported E-notation may use `e` or `E`; the exponent sign is optional, and
leading zeroes in the exponent are accepted. Whitespace may surround the field,
but not split the exponent marker from its exponent.

\snippet tests/test_data_type.cpp Scientific Notation Floats

### Decimal separators

csv::CSVField::try_parse_decimal() exists for CSV files that use a decimal
separator other than `.`. This is commonly needed for comma-decimal values such
as `3,14`. It produces a `long double` and keeps the normal field classification
visible on the CSVField.

\snippet tests/test_csv_field.cpp CSVField Decimal Separator Conversion

## Booleans

Boolean conversion is deliberately narrow. `true` and `false` are accepted
case-insensitively. Numeric values such as `1` are not implicitly converted to
`true`.

\snippet tests/test_csv_field.cpp CSVField Bool Conversion

## Timestamps

Timestamp classification supports ISO 8601-style timestamps such as
`1970-01-02T00:00:00.123Z`. csv::CSVField::try_parse_timestamp() returns Unix
time in milliseconds for `uint64_t`. Users can also convert to
`std::chrono::duration` and `std::chrono::system_clock::time_point`.

Integer fields can be used with csv::CSVField::try_parse_timestamp(), which lets
callers coerce Unix millisecond values into chrono targets explicitly.

\snippet tests/test_csv_field.cpp CSVField Timestamp Conversion

## std::optional and std::expected

The \ref CSVField_optional_conversion "std::optional conversion operator" is a
concise wrapper over csv::CSVField::try_get().
csv::CSVField::as() is the structured-error alternative for callers who need to
distinguish not-a-number, overflow, float-to-int, and negative-to-unsigned
failures.

\snippet tests/test_csv_field.cpp CSVField Optional Conversion
\snippet tests/test_csv_field.cpp CSVField Expected Conversion
