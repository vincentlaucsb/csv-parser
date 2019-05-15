// Tests for GiantStringBuffer

#include "catch.hpp"
#include "csv.hpp"
using namespace csv::internals;

TEST_CASE("GiantStringBufferTest", "[test_giant_string_buffer]") {
    GiantStringBuffer buffer;

    buffer.buffer.append("1234");
    std::string first_row = std::string(buffer.get_row());

    buffer.buffer.append("5678");
    std::string second_row = std::string(buffer.get_row());

    buffer.reset();
    buffer.buffer.append("abcd");
    std::string third_row = std::string(buffer.get_row());

    REQUIRE(first_row == "1234");
    REQUIRE(second_row == "5678");
    REQUIRE(third_row == "abcd");
}

TEST_CASE("GiantSplitBufferTest", "[test_giant_split_buffer]") {
    GiantSplitBuffer buffer;

    ColumnPositions * first = buffer.append(std::vector<unsigned short>({ 10, 20, 30, 40 })),
                    * second = buffer.append(std::vector<unsigned short>({ 1, 2, 3, 5, 8, 11 }));

    REQUIRE(first->n_cols == 4);
    REQUIRE(first->operator[](3) == 40);

    REQUIRE(second->n_cols == 6);
    REQUIRE(second->operator[](3) == 5);
}