#include <catch2/catch_all.hpp>
#include <csv.hpp>

#ifdef CSV_HAS_CXX20
#include <ranges>

TEST_CASE("CSVReader C++20 Ranges Compatibility", "[ranges][cxx20]") {
    SECTION("CSVReader works with std::ranges::distance") {
        std::stringstream ss("A,B,C\n1,2,3\n4,5,6\n7,8,9");
        csv::CSVReader reader(ss);

        auto count = std::ranges::distance(reader);
        REQUIRE(count == 3);
    }

    SECTION("CSVReader works with std::views") {
        std::stringstream ss("A,B,C\n1,2,3\n4,5,6\n7,8,9\n10,11,12");
        csv::CSVReader reader(ss);

        auto filtered = reader |
                        std::views::filter([](const csv::CSVRow &row) {
                            return !row.empty() && row[0].get<int>() > 5;
                        });

        int filtered_count = 0;
        for (const auto &row : filtered) {
            filtered_count++;
            int val = row[0].get<int>();
            REQUIRE(val > 5);
        }
        REQUIRE(filtered_count == 2); // rows with 7 and 10
    }

    SECTION("CSVReader iterator satisfies input_range requirements") {
        std::stringstream ss("A,B\n1,2\n3,4");
        csv::CSVReader reader(ss);

        auto it = reader.begin();
        auto end = reader.end();

        static_assert(std::input_iterator<decltype(it)>);
        static_assert(std::ranges::range<csv::CSVReader>);
        static_assert(std::ranges::input_range<csv::CSVReader>);
        static_assert(std::sentinel_for<decltype(end), decltype(it)>);

        REQUIRE(it != end);
        auto row = *it;
        REQUIRE(row.size() == 2);

        ++it;
        REQUIRE(it != end);

        ++it;
        REQUIRE(it == end);
    }
}
#endif
