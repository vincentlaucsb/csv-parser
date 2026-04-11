#include <catch2/catch_all.hpp>
#include "internal/basic_csv_parser.hpp"

#include <string>

using namespace csv;
using namespace csv::internals;

// find_next_non_special is SIMD-only: it fast-forwards through complete lanes
// that contain no sentinel bytes, then returns. The caller's scalar loop handles
// the sub-lane tail. When data is shorter than one lane, the function is a noop.

TEST_CASE("SIMD skip returns end for lane-aligned non-special run", "[simd][parser]") {
    // 512 bytes is a multiple of both AVX2 (32) and SSE2 (16) lane widths,
    // so the function processes every byte and reaches the end.
    const SentinelVecs sentinels(',', '"');

    std::string data(512, 'a');
    const auto pos = find_next_non_special(data, 0, sentinels);

    REQUIRE(pos == data.size());
}

TEST_CASE("SIMD skip returns pos unchanged when data shorter than one lane", "[simd][parser]") {
    // 8 bytes < minimum SIMD lane width (16 for SSE2). No complete lane to
    // process — function returns the start pos immediately. The caller's
    // scalar loop is responsible for these bytes.
    const SentinelVecs sentinels(',', '"');

    std::string data(8, 'x');
    const auto pos = find_next_non_special(data, 3, sentinels);

    REQUIRE(pos == 3);
}

TEST_CASE("SIMD skip finds delimiter at key boundary offsets", "[simd][parser]") {
    const SentinelVecs sentinels(',', '"');

    const size_t target = GENERATE(0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 95u);

    std::string data(128, 'x');
    data[target] = ',';

    const auto pos = find_next_non_special(data, 0, sentinels);
    REQUIRE(pos == target);
}

TEST_CASE("SIMD skip respects start offset", "[simd][parser]") {
    const SentinelVecs sentinels(',', '"');

    std::string data(160, 'z');
    data[12] = ',';
    data[97] = ',';

    const auto pos = find_next_non_special(data, 20, sentinels);
    REQUIRE(pos == 97);
}

TEST_CASE("SIMD skip handles custom delimiter and quote", "[simd][parser]") {
    const SentinelVecs sentinels('|', '~');

    std::string data(128, 'm');
    data[41] = '|';
    data[88] = '~';

    SECTION("Custom delimiter found first") {
        const auto pos = find_next_non_special(data, 0, sentinels);
        REQUIRE(pos == 41);
    }

    SECTION("Custom quote found from later start") {
        const auto pos = find_next_non_special(data, 42, sentinels);
        REQUIRE(pos == 88);
    }
}

TEST_CASE("SentinelVecs with dummy quote does not stop on quote bytes", "[simd][parser]") {
    // Simulates no_quote mode: SentinelVecs constructed with delimiter as dummy
    // for the quote slot. SIMD must not stop at '"' bytes because v_quote
    // broadcasts ',' (same as v_delim), so only real ',' bytes halt the scan.
    // 128 bytes with sentinel at 99 is well within SIMD range.
    const SentinelVecs sentinels(',', ',');

    std::string data(128, 'a');
    data[10] = '"';   // quote chars scattered through field -- must NOT stop SIMD
    data[33] = '"';
    data[65] = '"';
    data[99] = ',';   // real delimiter -- must stop here

    const auto pos = find_next_non_special(data, 0, sentinels);
    REQUIRE(pos == 99);
}

TEST_CASE("SIMD skip treats UTF-8 bytes as non-special content", "[simd][parser]") {
    const SentinelVecs sentinels(',', '"');

    std::string data;
    data.reserve(192);

    for (size_t i = 0; i < 64; ++i) {
        data.push_back(static_cast<char>(0xC3));
        data.push_back(static_cast<char>(0xA9));
        data.push_back('a');
    }

    const auto pos = find_next_non_special(data, 0, sentinels);
    REQUIRE(pos == data.size());
}
