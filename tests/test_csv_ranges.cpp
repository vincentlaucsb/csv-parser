#include <catch2/catch_all.hpp>
#include <csv.hpp>

#ifdef CSV_HAS_CXX20
#include <ranges>

#if CSV_EXCEPTIONS_ENABLED

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
#endif

#ifdef CSV_HAS_CXX20
static_assert(std::random_access_iterator<csv::CSVRow::iterator>);
static_assert(std::random_access_iterator<csv::CSVRow::reverse_iterator>);
static_assert(std::random_access_iterator<std::reverse_iterator<csv::CSVRow::iterator>>);
static_assert(std::ranges::random_access_range<csv::CSVRow>);
static_assert(std::ranges::random_access_range<const csv::CSVRow>);
static_assert(std::ranges::common_range<csv::CSVRow>);
static_assert(std::ranges::sized_range<csv::CSVRow>);
static_assert(!std::permutable<csv::CSVRow::iterator>);

TEST_CASE("CSVRow field values work with standard reverse adaptors", "[ranges][cxx20][test_csv_row_iter]") {
    auto data = std::make_shared<csv::internals::RawCSVData>();
    data->data = "123,234,345";
    for (size_t offset : {size_t(0), size_t(4), size_t(8)})
        data->fields.emplace_back(offset, size_t(3));
    csv::CSVRow row(data, 0, 0, 3);

    SECTION("Standard reverse iterator dereference, arrow, indexing and base") {
        auto it = std::make_reverse_iterator(row.end());
        auto last = *it;
        REQUIRE(last.get_sv() == "345");
        REQUIRE(it->get_sv() == "345");
        REQUIRE(it[1].get_sv() == "234");
        REQUIRE((1 + it)->get_sv() == "234");
        REQUIRE(it.base() == row.end());
        REQUIRE((*it++).get_sv() == "345");
        REQUIRE(it->get_sv() == "234");
        REQUIRE(last.get_sv() == "345");
    }

    SECTION("Reverse view and composed views") {
        auto reversed = row | std::views::reverse;
        auto it = reversed.begin();
        auto last = *it;
        REQUIRE(last.get_sv() == "345");
        REQUIRE(it->get_sv() == "345");
        REQUIRE(reversed[2].get_sv() == "123");
        REQUIRE(std::ranges::distance(reversed) == 3);
        auto strings = reversed
            | std::views::drop(1)
            | std::views::transform([](csv::CSVField field) { return std::string(field.get_sv()); });
        std::vector<std::string> values;
        std::ranges::copy(strings, std::back_inserter(values));
        REQUIRE(values == std::vector<std::string>{"234", "123"});
        auto forward = reversed | std::views::reverse;
        REQUIRE((*forward.begin()).get_sv() == "123");
        REQUIRE(last.get_sv() == "345");
    }

    SECTION("Filter, subrange and algorithms accept field values") {
        auto filtered = row | std::views::filter([](csv::CSVField field) {
            return field.get_sv() != "234";
        }) | std::views::reverse;
        REQUIRE((*filtered.begin()).get_sv() == "345");
        REQUIRE((*std::ranges::next(filtered.begin())).get_sv() == "123");
        auto range = std::ranges::subrange(row.begin(), row.end());
        auto reverse = range | std::views::reverse;
        REQUIRE(reverse.begin()->get_sv() == "345");
        auto max = std::ranges::max_element(row, {}, [](csv::CSVField field) { return field.get_sv(); });
        REQUIRE(max == row.begin() + 2);
        REQUIRE((*max).get_sv() == "345");
    }

    SECTION("Adaptor iterators retain row coordinates and storage") {
        auto reversed = row | std::views::reverse;
        auto it = reversed.begin();
        row = csv::CSVRow();
        data.reset();
        REQUIRE((*it).get_sv() == "345");
        REQUIRE((++it)->get_sv() == "234");
        REQUIRE(it[1].get_sv() == "123");
    }

    SECTION("Const field references in forward and reverse views") {
        const csv::CSVRow& const_row = row;
        int forward_sum = 0;
        for (const auto& field : const_row) {
            REQUIRE(field.is_num());
            int value = 0;
            REQUIRE(field.try_get(value));
            forward_sum += value;
#if CSV_EXCEPTIONS_ENABLED
            REQUIRE(field.get<int>() == value);
#endif
        }
        REQUIRE(forward_sum == 702);
        std::vector<int> values;
        for (const auto& field : const_row | std::views::reverse) {
            REQUIRE(field.is_int());
            int value = 0;
            REQUIRE(field.try_get(value));
            values.push_back(value);
        }
        REQUIRE(values == std::vector<int>{345, 234, 123});
    }

    SECTION("Const, empty and single-field reverse views") {
        const csv::CSVRow& const_row = row;
        auto reversed = const_row | std::views::reverse;
        REQUIRE(reversed.begin()->get_sv() == "345");
        csv::CSVRow empty;
        auto empty_reverse = empty | std::views::reverse;
        REQUIRE(empty_reverse.begin() == empty_reverse.end());
        csv::CSVRow single(data, 0, 0, 1);
        auto single_reverse = single | std::views::reverse;
        REQUIRE(single_reverse.begin()->get_sv() == "123");
        REQUIRE(++single_reverse.begin() == single_reverse.end());
    }
}
#endif
