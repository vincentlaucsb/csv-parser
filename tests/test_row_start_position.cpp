/** @file
 *  Tests for CSV parsing
 */

#include <stdio.h>  // remove()
#include <unistd.h>

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <ios>
#include <sstream>

#include "csv.hpp"

using namespace csv;
using std::string;
using std::vector;

//
// CSVRow::current_row_start()
//

TEST_CASE("CSVRow::current_row_start", "[current_row_start]") {
    CSVGuessResult guessed_format = guess_format("./tests/data/real_data/YEAR07_CBSA_NAC3.txt");
    REQUIRE(guessed_format.delim == ';');
    REQUIRE(guessed_format.header_row == 0);

    std::fstream fstream;
    auto testfile = std::filesystem::path("./tests/data/real_data/YEAR07_CBSA_NAC3.txt");
    std::ifstream ifs(testfile.c_str());
    std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));

    CSVFormat format;
    format.delimiter(guessed_format.delim).header_row(guessed_format.header_row);

    {
        // parse  from file
        CSVReader reader(testfile.c_str(), format);
        uint64_t pos = 0;
        for (CSVRow& row : reader) {
            pos = content.find_first_of('\n', pos) + 1;
            REQUIRE(row.current_row_start() == pos);
        }
    }

    {
        // parse from stream
        auto stream = std::stringstream(content);
        auto reader = CSVReader(stream, format);

        uint64_t pos = 0;
        for (CSVRow& row : reader) {
            pos = content.find_first_of('\n', pos) + 1;
            REQUIRE(row.current_row_start() == pos);
        }
    }
}
