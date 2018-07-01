//
// Tests for the CSVRow Iterators and CSVReader Iterators
//

#include "catch.hpp"
#include "csv_parser.hpp"
using namespace csv;

auto make_csv_row();
auto make_csv_row() {
    auto rows = "A,B,C\r\n" // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3"_csv;

    return rows.front();
}

//////////////////////
// CSVRow Iterators //
//////////////////////

TEST_CASE("Test CSVRow Iterator", "[csv_iter]") {
    auto row = make_csv_row();

    REQUIRE(row.begin()->get<int>() == 123);
    REQUIRE(row.end()->get<>() == "345");

    size_t i = 0;
    for (auto it = row.begin(); it != row.end(); ++it) {
        if (i == 0) REQUIRE(it->get<>() == "123");
        else if (i == 1) REQUIRE(it->get<>() == "234");
        else  REQUIRE(it->get<>() == "345");

        i++;
    }
}

TEST_CASE("Test CSVRow Iterator Arithmetic", "[csv_iter_math]") {
    auto row = make_csv_row();

    REQUIRE(row.begin()->get<int>() == 123);
    REQUIRE(row.end()->get<>() == "345");

    auto row_start = row.begin();
    REQUIRE(*(row_start + 1) == "234");
    REQUIRE(*(row_start + 2) == "345");

}

TEST_CASE("Test CSVRow Range Based For", "[csv_iter_for]") {
    auto row = make_csv_row();

    size_t i = 0;
    for (auto& field: row) {
        if (i == 0) REQUIRE(field.get<>() == "123");
        else if (i == 1) REQUIRE(field.get<>() == "234");
        else  REQUIRE(field.get<>() == "345");

        i++;
    }
}

/////////////////////////
// CSVReader Iterators //
/////////////////////////

TEST_CASE("Basic CSVReader Iterator Test", "[read_ints_iter]") {
    // A file where each value in the ith row is the number i
    // There are 100 rows
    CSVReader reader("./tests/data/fake_data/ints.csv");
    
    auto it = reader.begin();
    for (size_t i = 1; it != reader.end(); i++) {
        REQUIRE((*it)[0].get<int>() == i);
        it++;
    }
}

TEST_CASE("CSVReader Iterator + std::find", "[find_ints]") {
    // A file where each value in the ith row is the number i
    // There are 100 rows
    CSVReader r1("./tests/data/fake_data/ints.csv"),
        r2("./tests/data/real_data/2015_StateDepartment.csv");
    
    auto int_finder = [](CSVRow& left, CSVRow& right) {
        return (left["A"].get<int>() < right["A"].get<int>());
    };

    auto wage_finder = [](CSVRow& left, CSVRow& right) {
        return (left["Total Wages"].get<double>() < right["Total Wages"].get<double>());
    };

    auto max_int = std::max_element(r1.begin(), r2.end(), int_finder);
    auto max_wage = std::max_element(r2.begin(), r2.end(), wage_finder);

    REQUIRE((*max_int)["A"].get<>() == "100");
    REQUIRE((*max_wage)["Total Wages"].get<>() == "812064.87");
}