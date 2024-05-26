#include <string>
#include <tuple>

using std::make_tuple;

namespace csv_test {
    static const std::initializer_list<std::tuple<std::string, long double>> FLOAT_TEST_CASES = {
        make_tuple("3.14", 3.14L),
        make_tuple("+3.14", 3.14L),
        make_tuple("       -3.14            ", -3.14L),
        make_tuple("2.71828", 2.71828L),

        // Test uniform distribution values
        make_tuple("0.12", 0.12L),
        make_tuple("0.334", 0.334L),
        make_tuple("0.625", 0.625L),
        make_tuple("0.666666", 0.666666L),
        make_tuple("0.69", 0.69L),

        // Test negative values between 0 and 1
        make_tuple("-0.12", -0.12L),
        make_tuple("-0.334", -0.334L),
        make_tuple("-0.625", -0.625L),
        make_tuple("-0.666666", -0.666666L),
        make_tuple("-0.69", -0.69L),

        // Larger numbers
        make_tuple("1000.00", 1000L),
        make_tuple("1000000.00", 1000000L),
        make_tuple("9999999.99", 9999999.99L),
        make_tuple("99999999.999", 99999999.999L),

        make_tuple("-1000.00", -1000L),
        make_tuple("-1000000.00", -1000000L),
        make_tuple("-9999999.99", -9999999.99L),
        make_tuple("-99999999.999", -99999999.999L),
    };
}