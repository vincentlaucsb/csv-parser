// Tests for RawRowBuffer

#include "catch.hpp"
#include "csv.hpp"
using namespace csv::internals;

TEST_CASE("GiantStringBufferTest", "[test_giant_string_buffer]") {
    RawRowBuffer buffer;

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
    RawRowBuffer buffer;
    auto & splits = buffer.split_buffer;

    splits.push_back(1);
    splits.push_back(2);
    splits.push_back(3);

    auto pos = buffer.get_splits();
    REQUIRE(pos.split_at(0) == 1);
    REQUIRE(pos.split_at(1) == 2);
    REQUIRE(pos.split_at(2) == 3);
    REQUIRE(pos.n_cols == 4);

    splits.push_back(4);
    splits.push_back(5);

    pos = buffer.get_splits();
    REQUIRE(pos.split_at(0) == 4);
    REQUIRE(pos.split_at(1) == 5);
    REQUIRE(pos.n_cols == 3);
}