// Tests for GiantStringBuffer

#include "catch.hpp"
#include "csv_parser.hpp"
using namespace csv::internals;

TEST_CASE("GiantStringBufferTest", "[test_giant_string_buffer]") {
    GiantStringBuffer buffer;

    buffer->append("1234");
    std::string first_row = std::string(buffer.get_row());

    buffer->append("5678");
    std::string second_row = std::string(buffer.get_row());

    buffer.reset();
    buffer->append("abcd");
    std::string third_row = std::string(buffer.get_row());

    REQUIRE(first_row == "1234");
    REQUIRE(second_row == "5678");
    REQUIRE(third_row == "abcd");
}