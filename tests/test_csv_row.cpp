#include "csv.hpp"

#if CSV_EXCEPTIONS_ENABLED
// Tests for the CSVRow and CSVField Data Structures

#include <catch2/catch_all.hpp>
#include "csv.hpp"
using namespace csv;

// Construct a CSVRow and assert that its interface works as expected
TEST_CASE("CSVRow Test", "[test_csv_row]") {
    auto reader = "A,B,C,D\r\n"
                  "Col1,Col2,Col3,Col4"_csv;

    CSVRow row;
    reader.read_row(row);
    
    bool error_caught = false;

    SECTION("size() Check") {
        REQUIRE(row.size() == 4);
    }

    SECTION("operator[]") {
        REQUIRE(row[1] == "Col2");
        REQUIRE(row["B"] == "Col2");

        REQUIRE(row[2] == "Col3");
        REQUIRE(row["C"] == "Col3");
    }

    SECTION("operator[] Out of Bounds") {
        try {
            row[4].get<>();
        }
        catch (std::runtime_error&) {
            error_caught = true;
        }

        REQUIRE(error_caught);
    }

    SECTION("operator[] Access Non-Existent Column") {
        try {
            row["Col5"].get<>();
        }
        catch (std::runtime_error&) {
            error_caught = true;
        }

        REQUIRE(error_caught);
    }

    SECTION("Content Check") {
        REQUIRE(std::vector<std::string>(row) ==
            std::vector<std::string>({ "Col1", "Col2", "Col3", "Col4" }));
    }

    /** Allow get_sv() to be used with a const CSVField
     *  
     *  See: https://github.com/vincentlaucsb/csv-parser/issues/86
     *
     */
    SECTION("get_sv() Check") {
        std::vector<std::string> content;

        for (const auto& field : row) {
            content.push_back(std::string(field.get_sv()));
        }

        REQUIRE(std::vector<std::string>(row) ==
            std::vector<std::string>({ "Col1", "Col2", "Col3", "Col4" }));
    }
}

// Integration test for CSVRow/CSVField
TEST_CASE("CSVField operator==", "[test_csv_field_equal]") {
    auto reader = "A,B,C,D\r\n"
                  "1,2,3,3.14"_csv;

    CSVRow row;
    reader.read_row(row);

    REQUIRE(row["A"] == 1);
    REQUIRE(row["B"] == 2);
    REQUIRE(row["C"] == 3);
    REQUIRE(internals::is_equal(row["D"].get<long double>(), 3.14L));
}

#ifndef __EMSCRIPTEN__
TEST_CASE("CSVRow::to_unordered_map", "[test_csv_row]") {
    SECTION("Convert row to full map") {
        CSVReader reader("./tests/data/fake_data/ints_squared.csv");
        
        auto it = reader.begin();
        REQUIRE(it != reader.end());
        auto row_map = it->to_unordered_map();

        REQUIRE(row_map.size() == 2);
        REQUIRE(row_map.count("A") == 1);
        REQUIRE(row_map.count("B") == 1);

        // Verify values are correct
        int a_val = std::stoi(row_map["A"]);
        int b_val = std::stoi(row_map["B"]);
        REQUIRE(b_val == a_val * a_val);
    }

    SECTION("Convert row to subset map") {
        CSVReader reader("./tests/data/fake_data/ints_squared.csv");
        
        auto it = reader.begin();
        REQUIRE(it != reader.end());
        auto row_map = it->to_unordered_map({"A"});

        REQUIRE(row_map.size() == 1);
        REQUIRE(row_map.count("A") == 1);
        REQUIRE(row_map.count("B") == 0);
        REQUIRE(row_map["A"] == "1");
    }
}
#endif
#endif

#include "shared/explicit_error_helpers.hpp"

using namespace csv;

TEST_CASE("Checked map serialization preserves explicit empty subsets", "[explicit_errors]") {
    // Full-map and explicit-subset overloads must keep their distinct empty-subset semantics.
    CSVError error;
    auto reader = try_parse("left,right\n3,7\n", error);
    REQUIRE(reader);
    CSVRow row;
    REQUIRE(reader->try_read_row(row, error));
    REQUIRE(row.try_to_unordered_map(error).size() == 2);
    REQUIRE_FALSE(error);
    REQUIRE(row.try_to_unordered_map(error, {}).empty());
    REQUIRE_FALSE(error);
    REQUIRE(CSVRow().try_to_unordered_map(error).empty());
    REQUIRE_FALSE(error);
#if CSV_EXCEPTIONS_ENABLED
    REQUIRE(row.to_unordered_map({}).empty());
#endif
}

TEST_CASE("CSVRow value iterators preserve navigation and lifetimes", "[test_csv_row_iter]") {
    // Pure row-view regression: no parser path or large fixture is needed here.
    auto data = std::make_shared<internals::RawCSVData>();
    data->data = "123,234,345";
    for (size_t offset : {size_t(0), size_t(4), size_t(8)})
        data->fields.emplace_back(offset, size_t(3));
    CSVRow row(data, 0, 0, 3);

    SECTION("Dereference and indexing return independent field values") {
        auto it = row.begin();
        auto first = *it;
        auto indexed = it[1];
        ++it;
        REQUIRE(first.get_sv() == "123");
        REQUIRE(indexed.get_sv() == "234");
        REQUIRE(it[-1].get_sv() == "123");
        REQUIRE((2 + row.begin())->get_sv() == "345");
        auto arrow = it.operator->();
        ++it;
        REQUIRE(arrow->get_sv() == "234");
        const auto const_arrow = it.operator->();
        REQUIRE(const_arrow->get_sv() == "345");
    }

    SECTION("Forward arithmetic and ordering") {
        auto it = row.begin();
        REQUIRE((it++)->get_sv() == "123");
        REQUIRE((it--)->get_sv() == "234");
        REQUIRE((++it)->get_sv() == "234");
        REQUIRE((--it)->get_sv() == "123");
        REQUIRE(&(it += 3) == &it);
        REQUIRE(it == row.end());
        REQUIRE(&(it -= 2) == &it);
        REQUIRE(it - row.begin() == 1);
        REQUIRE(row.begin() - it == -1);
        REQUIRE(row.begin() < it);
        REQUIRE(it > row.begin());
        REQUIRE(it <= it);
        REQUIRE(it >= it);
        REQUIRE_FALSE(it < row.begin());
        REQUIRE_FALSE(it > row.end());
        REQUIRE_FALSE(it <= row.begin());
        REQUIRE_FALSE(it >= row.end());
        REQUIRE(std::distance(row.begin(), row.end()) == 3);
    }

    SECTION("Reverse navigation, indexing and ordering") {
        auto it = 1 + row.rbegin();
        REQUIRE(it[0].get_sv() == "234");
        REQUIRE(it[-1].get_sv() == "345");
        REQUIRE(it.base() == row.begin() + 2);
        auto middle = *it;
        REQUIRE((it--)->get_sv() == "234");
        REQUIRE(it->get_sv() == "345");
        REQUIRE((++it)->get_sv() == "234");
        it += 2;
        REQUIRE(it == row.rend());
        REQUIRE((--it)->get_sv() == "123");
        it -= 2;
        REQUIRE(it == row.rbegin());
        REQUIRE(middle.get_sv() == "234");
        REQUIRE(row.rbegin() - row.rend() == -3);
        REQUIRE(row.rbegin() < row.rend());
        REQUIRE(row.rend() > row.rbegin());
        REQUIRE(it <= it);
        REQUIRE(it >= it);
        REQUIRE_FALSE(row.rend() < it);
        REQUIRE_FALSE(it > row.rend());
        REQUIRE_FALSE(row.rend() <= it);
        REQUIRE_FALSE(it >= row.rend());
    }

    SECTION("Copied iterators and standard input algorithms") {
        auto it = row.rbegin();
        auto copy = it;
        ++it;
        REQUIRE(copy->get_sv() == "345");
        REQUIRE(it->get_sv() == "234");
        std::vector<CSVField> fields;
        std::copy(row.rbegin(), row.rend(), std::back_inserter(fields));
        REQUIRE(fields.size() == 3);
        REQUIRE(fields[0].get_sv() == "345");
        REQUIRE(fields[1].get_sv() == "234");
        REQUIRE(fields[2].get_sv() == "123");
        REQUIRE(std::find_if(row.begin(), row.end(), [](CSVField field) {
            return field.get_sv() == "234";
        }) == row.begin() + 1);
    }

    SECTION("The original row can be reassigned and destroyed") {
        auto it = row.begin();
        auto reverse = row.rbegin();
        row = CSVRow();
        data.reset();
        REQUIRE((*it).get_sv() == "123");
        REQUIRE((++it)->get_sv() == "234");
        REQUIRE(it[1].get_sv() == "345");
        REQUIRE((*reverse).get_sv() == "345");
        REQUIRE((++reverse)->get_sv() == "234");
    }

    SECTION("Default, empty, single-field and distinct-row iterators") {
        REQUIRE(CSVRow::iterator() == CSVRow::iterator());
        REQUIRE(CSVRow::reverse_iterator() == CSVRow::reverse_iterator());
        CSVRow empty;
        REQUIRE(empty.begin() == empty.end());
        REQUIRE(empty.rbegin() == empty.rend());
        REQUIRE(empty.rend() - empty.rbegin() == 0);
        CSVRow single(data, 4, 1, 1);
        REQUIRE(single.begin() != row.begin());
        auto it = single.rbegin();
        REQUIRE(it[0].get_sv() == "345");
        REQUIRE(++it == single.rend());
        REQUIRE((--it)->get_sv() == "345");
        auto other_data = std::make_shared<internals::RawCSVData>();
        CSVRow other(other_data, 0, 0, 3);
        REQUIRE(other.begin() != row.begin());
    }
}

TEST_CASE("CSVRow iterator fields keep lazy parsing state local", "[test_csv_row_iter]") {
    auto data = std::make_shared<internals::RawCSVData>();
    data->data = "1,5";
    data->fields.emplace_back(size_t(0), size_t(3));
    CSVRow row(data, 0, 0, 1);
    auto field = *row.begin();
    long double value = 0;
    REQUIRE(field.try_parse_decimal(value, ','));
    REQUIRE(value == 1.5L);
    REQUIRE(field.is_float());
    // A fresh dereference must not inherit the previous wrapper's decimal policy.
    REQUIRE((*row.begin()).is_str());
    REQUIRE((*row.rbegin()).is_str());
}

TEST_CASE("CSVRow iterator snapshots preserve decoding and scalar slots", "[test_csv_row_iter]") {
    SECTION("Different row offsets and eager scalar positions in the same chunk") {
        auto data = std::make_shared<internals::RawCSVData>();
        data->data = "111,222\n333,444\n";
        for (int value : {111, 222, 333, 444}) {
            data->fields.emplace_back(data->fields.size() % 2 == 0 ? size_t(0) : size_t(4), size_t(3));
            internals::CSVFieldScalar scalar;
            scalar.type = DataType::CSV_INT16;
            scalar.integer = value;
            data->field_scalars.emplace_back(scalar);
        }
        CSVRow first(data, 0, 0, 2);
        CSVRow second(data, 8, 2, 2);
        auto it = second.begin();
        auto reverse = second.rbegin();
        second = first;
        int value = 0;
        auto third = *it;
        REQUIRE(third.get_sv() == "333");
        REQUIRE(third.try_get(value));
        REQUIRE(value == 333);
        REQUIRE((++it)->get_sv() == "444");
        REQUIRE((*reverse).try_get(value));
        REQUIRE(value == 444);
        REQUIRE((++reverse)->get_sv() == "333");
    }
    SECTION("Realized quoted text and whitespace trimming") {
        auto data = std::make_shared<internals::RawCSVData>();
        const auto offset = data->quote_arena.append("  a\"b  ");
        data->fields.emplace_back(offset, size_t(7), true);
        data->has_ws_trimming = true;
        data->ws_flags = internals::make_ws_flags({' '});
        CSVRow row(data, 0, 0, 1);
        REQUIRE(row.begin()->get_sv() == "a\"b");
        REQUIRE((*row.rbegin()).get_sv() == "a\"b");
    }
}

TEST_CASE("CSVRow iterators retain their span across chunk transitions", "[test_csv_row_iter]") {
    using namespace csv_test::explicit_errors;
    const auto& filename = large_csv(false);
    auto validate = [](CSVReader& reader) {
        CSVError error;
        CSVRow row;
        REQUIRE(reader.try_read_row(row, error));
        auto first = row.begin();
        auto last = row.rbegin();
        size_t count = 1;
        while (reader.try_read_row(row, error)) ++count;
        REQUIRE_FALSE(error);
        REQUIRE(count == large_row_count);
        row = CSVRow();
        REQUIRE(first[0].get_sv() == "0");
        REQUIRE((++first)->get_sv() == "1");
        REQUIRE(last[0].get_sv() == "4");
        REQUIRE((++last)->get_sv() == "3");
    };
    CSVError error;
    SECTION("Memory-mapped path") {
        auto reader = CSVReader::try_open(filename, error);
        REQUIRE(reader);
        validate(*reader);
    }
    SECTION("Stream path") {
        std::ifstream input(filename, std::ios::binary);
        auto reader = CSVReader::try_open(input, error);
        REQUIRE(reader);
        validate(*reader);
    }
}
