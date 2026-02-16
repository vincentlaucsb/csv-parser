//
// Tests for the CSVRow Iterators and CSVReader Iterators
//

#include <catch2/catch_all.hpp>
#include "csv.hpp"
using namespace csv;

//////////////////////
// CSVRow Iterators //
//////////////////////

TEST_CASE("Test CSVRow Interator", "[test_csv_row_iter]") {
    auto rows = "A,B,C\r\n" // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3"_csv;

    CSVRow row;
    rows.read_row(row);

    SECTION("Forwards and Backwards Iterators") {
        // Forwards
        REQUIRE(row.begin()->get<int>() == 123);
        REQUIRE((row.end() - 1)->get<>() == "345");

        size_t i = 0;
        for (auto it = row.begin(); it != row.end(); ++it) {
            if (i == 0) REQUIRE(it->get<>() == "123");
            else if (i == 1) REQUIRE(it->get<>() == "234");
            else  REQUIRE(it->get<>() == "345");

            i++;
        }

        // Backwards
        REQUIRE(row.rbegin()->get<int>() == 345);
        REQUIRE((row.rend() - 1)->get<>() == "123");
    }

    SECTION("Iterator Arithmetic") {
        REQUIRE(row.begin()->get<int>() == 123);
        REQUIRE((row.end() - 1)->get<>() == "345");

        auto row_start = row.begin();
        REQUIRE(*(row_start + 1) == "234");
        REQUIRE(*(row_start + 2) == "345");

    }

    SECTION("Post-Increment Iterator") {
        auto it = row.begin();

        REQUIRE(it++->get<int>() == 123);
        REQUIRE(it->get<int>() == 234);

        REQUIRE(it--->get<int>() == 234);
        REQUIRE(it->get<int>() == 123);
    }

    SECTION("Range Based For") {
        size_t i = 0;
        for (auto& field : row) {
            if (i == 0) REQUIRE(field.get<>() == "123");
            else if (i == 1) REQUIRE(field.get<>() == "234");
            else  REQUIRE(field.get<>() == "345");

            i++;
        }
    }
}

/////////////////////////
// CSVReader Iterators //
/////////////////////////

//! [CSVReader Iterator 1]
TEST_CASE("Basic CSVReader Iterator Test", "[read_ints_iter]") {
    // A file with 100 rows and columns A, B, ... J
    // where every value in the ith row is the number i
    CSVReader reader("./tests/data/fake_data/ints.csv");
    std::vector<std::string> col_names = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J"
    };
    int i = 1;

    SECTION("Basic Iterator") {
        for (auto it = reader.begin(); it != reader.end(); ++it) {
            REQUIRE((*it)[0].get<int>() == i);
            i++;
        }
    }

    SECTION("Iterator Post-Increment") {
        auto it = reader.begin();
        REQUIRE((it++)->operator[]("A").get<int>() == 1);
        REQUIRE(it->operator[]("A").get<int>() == 2);
    }

    SECTION("Range-Based For Loop") {
        for (auto& row : reader) {
            for (auto& j : col_names) REQUIRE(row[j].get<int>() == i);
            i++;
        }
    }
}
//! [CSVReader Iterator 1]

//! [CSVReader Iterator 2]
/**
 * IMPORTANT: CSVReader::iterator is std::input_iterator_tag (single-pass)
 * to support streaming large files with bounded memory usage.
 * 
 * Algorithms requiring ForwardIterator (like std::max_element) may appear
 * to work with small files, but cause heap-use-after-free when data spans
 * multiple RawCSVData chunks that get freed as the iterator advances.
 * 
 * CORRECT approach: Copy to vector first, then use algorithms.
 */
TEST_CASE("CSVReader Iterator + Algorithms Requiring ForwardIterator", "[iter_algorithms]") {
    SECTION("std::max_element - CORRECT approach using vector") {
        // The first is such that each value in the ith row is the number i
        // There are 100 rows
        CSVReader reader("./tests/data/fake_data/ints.csv");

        // Copy rows to vector to enable ForwardIterator algorithms
        auto rows = std::vector<CSVRow>(reader.begin(), reader.end());
        REQUIRE(rows.size() == 100);

        // Find largest number
        auto int_finder = [](const CSVRow& left, const CSVRow& right) {
            return (left["A"].get<int>() < right["A"].get<int>());
        };

        auto max_int = std::max_element(rows.begin(), rows.end(), int_finder);
        REQUIRE((*max_int)["A"] == 100);
    }

    SECTION("std::max_element - Large File using vector") {
        // The second file is a database of California state employee salaries
        CSVReader reader("./tests/data/real_data/2015_StateDepartment.csv");
        
        // Copy rows to vector to enable ForwardIterator algorithms
        auto rows = std::vector<CSVRow>(reader.begin(), reader.end());
        
        // Find highest salary
        auto wage_finder = [](const CSVRow& left, const CSVRow& right) {
            return (left["Total Wages"].get<double>() < right["Total Wages"].get<double>());
        };

        auto max_wage = std::max_element(rows.begin(), rows.end(), wage_finder);

        REQUIRE((*max_wage)["Total Wages"] == "812064.87");
    }
}
//! [CSVReader Iterator 2]
