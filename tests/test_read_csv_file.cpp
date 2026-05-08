/** @file
 *  Tests for CSV parsing
 */

#include <cstdint>
#include <fstream>
#include <sstream>
#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/file_guard.hpp"
#include "shared/generated_file.hpp"

using namespace csv;
using std::vector;
using std::string;

namespace {
#if !defined(__EMSCRIPTEN__) && CSV_ENABLE_THREADS
    const std::string& speculative_read_chunk_flags_filename() {
        static csv_test::GeneratedFile file("./tests/data/tmp_speculative_read_chunk_flags.csv");

        return file.path([](std::ofstream& out) {
            out << "id,text,tail\n";

            const size_t rows_to_write = 100000;
            for (size_t i = 0; i < rows_to_write; ++i) {
                out << i << ",  \"literal_" << i << "\"  ,tail_" << i << "\n";
                if (i == 6000) {
                    out << "short,only_two_columns\n";
                }
                if (i == 17000) {
                    out << "long,has,one,extra_column\n";
                }
            }
        });
    }
#endif
}

#ifndef __EMSCRIPTEN__
TEST_CASE("col_pos() Test", "[test_col_pos]") {
    auto pos = get_col_pos(
        "./tests/data/real_data/2015_StateDepartment.csv",
        "Entity Type");
    REQUIRE(pos == 1);
}

TEST_CASE("Prevent Column Names From Being Overwritten", "[csv_col_names_overwrite]") {
    std::vector<std::string> column_names = { "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10" };
    
    // Test against a variety of different CSVFormat objects
    std::vector<CSVFormat> formats = {};
    formats.push_back(CSVFormat::guess_csv());
    formats.push_back(CSVFormat());
    formats.back().delimiter(std::vector<char>({ ',', '\t', '|'}));
    formats.push_back(CSVFormat());
    formats.back().delimiter(std::vector<char>({ ',', '~'}));

    for (auto& format_in : formats) {
        // Set up the CSVReader
        format_in.column_names(column_names);
        CSVReader reader("./tests/data/fake_data/ints_comments.csv", format_in);

        // Assert that column names weren't overwritten
        CSVFormat format_out = reader.get_format();
        REQUIRE(reader.get_col_names() == column_names);
        REQUIRE(format_out.get_delim() == ',');
        REQUIRE(format_out.get_header() == 5);
    }
}
#endif

// get_file_info()
#ifndef __EMSCRIPTEN__
TEST_CASE("get_file_info() Test", "[test_file_info]") {
    SECTION("ints.csv") {
        CSVFileInfo info = get_file_info(
            "./tests/data/fake_data/ints.csv");

        REQUIRE(info.delim == ',');
        REQUIRE(info.n_rows == 100);
    }

    SECTION("2009PowerStatus.txt") {
        CSVFileInfo info = get_file_info(
            "./tests/data/real_data/2009PowerStatus.txt");

        REQUIRE(info.delim == '|');
        REQUIRE(info.n_rows == 37960); // Can confirm with Excel
        REQUIRE(info.n_cols == 3);
        REQUIRE(info.col_names == vector<string>({ "ReportDt", "Unit", "Power" }));
    }
}

TEST_CASE("Non-Existent CSV", "[read_ghost_csv]") {
    // Make sure attempting to parse a non-existent CSV throws an error
    bool error_caught = false;

    try {
        CSVReader reader("./lochness.csv");
    }
    catch (std::runtime_error& err) {
        error_caught = true;
        REQUIRE(err.what() == internals::make_prefixed_message(
            internals::ERROR_CANNOT_OPEN_FILE,
            "./lochness.csv"
        ));
    }

    REQUIRE(error_caught);
}
#endif

#ifndef __EMSCRIPTEN__
TEST_CASE("Test Read CSV where file does NOT end with newline", "[test_file_info_ints2]") {
    CSVReader reader("./tests/data/fake_data/ints_doesnt_end_in_newline.csv");

    auto row = reader.begin();
    for (; row != reader.end(); row++) {} // skip to end

    REQUIRE((*row)["A"] == 100);
    REQUIRE((*row)["J"] == 100);
}

TEST_CASE( "Test Read CSV with Header Row", "[read_csv_header]" ) {
    // Header on first row
    constexpr auto path = "./tests/data/real_data/2015_StateDepartment.csv";

    // Expected Results
    vector<string> col_names = {
        "Year", "Entity Type", "Entity Group", "Entity Name",
        "Department / Subdivision", "Position", "Elected Official",
        "Judicial", "Other Positions", "Min Classification Salary",
        "Max Classification Salary", "Reported Base Wage", "Regular Pay",
        "Overtime Pay", "Lump-Sum Pay", "Other Pay", "Total Wages",
        "Defined Benefit Plan Contribution", "Employees Retirement Cost Covered",
        "Deferred Compensation Plan", "Health Dental Vision",
        "Total Retirement and Health Cost", "Pension Formula",
        "Entity URL", "Entity Population", "Last Updated",
        "Entity County", "Special District Activities"
    };

    vector<string> first_row = {
        "2015","State Department","","Administrative Law, Office of","",
        "Assistant Chief Counsel","False","False","","112044","129780",""
        ,"133020.06","0","2551.59","2434.8","138006.45","34128.65","0","0"
        ,"15273.97","49402.62","2.00% @ 55","http://www.spb.ca.gov/","",
        "08/02/2016","",""
    };

    // Test logic extracted to lambda to avoid CSVReader copy constructor issue
    auto test_reader = [&](CSVReader& reader) {
        CSVRow row;
        reader.read_row(row); // Populate row with first line

        REQUIRE(vector<string>(row) == first_row);
        REQUIRE(reader.get_col_names() == col_names);

        // Skip to end
        while (reader.read_row(row));
        REQUIRE(reader.n_rows() == 246497);
    };

    SECTION("Memory mapped file") {
        CSVReader reader(path, CSVFormat());
        test_reader(reader);
    }

    SECTION("std::ifstream") {
        std::ifstream infile(path, std::ios::binary);
        CSVReader reader(infile, CSVFormat());
        test_reader(reader);
    }
}

//
// read_row()
//
//! [CSVField Example]
TEST_CASE("Test read_row() CSVField - Easy", "[read_row_csvf1]") {
    // Test that integers are type-casted properly
    CSVReader reader("./tests/data/fake_data/ints.csv");
    CSVRow row;

    while (reader.read_row(row)) {
        for (size_t i = 0; i < row.size(); i++) {
            REQUIRE(row[i].is_int());
            REQUIRE(row[i].get<int>() <= 100);
        }
    }
}
//! [CSVField Example]

TEST_CASE("Test read_row() CSVField - Power Status", "[read_row_csvf3]") {
    CSVReader reader("./tests/data/real_data/2009PowerStatus.txt");
    CSVRow row;

    size_t date = reader.index_of("ReportDt"),
        unit = reader.index_of("Unit"),
        power = reader.index_of("Power");
    
    // Try to find a non-existent column
    REQUIRE(reader.index_of("metallica") == CSV_NOT_FOUND);

    for (size_t i = 0; reader.read_row(row); i++) {
        // Assert correct types
        REQUIRE(row[date].is_str());
        REQUIRE(row[unit].is_str());
        REQUIRE(row[power].is_int());

        // Spot check
        if (i == 2) {
            REQUIRE(row[power].get<int>() == 100);
            REQUIRE(row[date].get<>() == "12/31/2009"); // string_view
            REQUIRE(row[unit].get<std::string>() == "Beaver Valley 1");
        }
    }
}

// Regression test for issue #149: trailing newline at EOF must not produce a spurious
// empty row when reading from an ifstream (mmap parser path).
TEST_CASE("Trailing newline at EOF (ifstream/mmap)", "[trailing_newline_ifstream]") {
    auto write_and_count = [](const std::string& filename, const std::string& content) -> size_t {
        FileGuard cleanup(filename);
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << content;
        out.close();

        CSVFormat format;
        format.no_header();
        CSVReader reader(cleanup.filename, format);
        size_t row_count = 0;
        for (auto& row : reader) {
            REQUIRE(row.size() > 0);
            row_count++;
        }
        return row_count;
    };

    REQUIRE(write_and_count("./tests/data/tmp_trailing_newline_crlf.csv", "A,B,C\r\n1,2,3\r\n") == 2);
    REQUIRE(write_and_count("./tests/data/tmp_trailing_newline_lf.csv", "A,B,C\n1,2,3\n") == 2);
    REQUIRE(write_and_count("./tests/data/tmp_trailing_newline_none.csv", "A,B,C\n1,2,3") == 2);
}

TEST_CASE("Trim regression: quoted unescape and bounded field slice", "[read_csv_trim][regression]") {
    FileGuard cleanup("./tests/data/tmp_trim_regression.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "A,B,C,D\n"
            << "x,\"  a\"\"b  \",y,z\n"
            << "  left   ,   mid   ,   right   ,   tail   \n";
    }

    CSVFormat format;
    format.header_row(0)
        .trim({ ' ', '\t' })
        .delimiter(',');

    auto validate_reader = [&](CSVReader& reader) {
        CSVRow row;

        REQUIRE(reader.read_row(row));
        REQUIRE(row.size() == 4);
        REQUIRE(row["A"].get<std::string>() == "x");
        REQUIRE(row["B"].get<std::string>() == "a\"b");
        REQUIRE(row["C"].get<std::string>() == "y");
        REQUIRE(row["D"].get<std::string>() == "z");

        REQUIRE(reader.read_row(row));
        REQUIRE(row.size() == 4);
        REQUIRE(row["A"].get<std::string>() == "left");
        REQUIRE(row["B"].get<std::string>() == "mid");
        REQUIRE(row["C"].get<std::string>() == "right");
        REQUIRE(row["D"].get<std::string>() == "tail");

        REQUIRE_FALSE(reader.read_row(row));
    };

    SECTION("Memory-mapped file path") {
        CSVReader reader(cleanup.filename, format);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, format);
        validate_reader(reader);
    }
}

namespace {
    bool points_into(csv::string_view view, csv::string_view owner) {
        if (view.empty() || owner.empty()) {
            return false;
        }

        const std::uintptr_t view_begin = reinterpret_cast<std::uintptr_t>(view.data());
        const std::uintptr_t owner_begin = reinterpret_cast<std::uintptr_t>(owner.data());
        return view_begin >= owner_begin && view_begin < owner_begin + owner.size();
    }
}

TEST_CASE("Doubled quote fields are realized without changing plain quoted views", "[read_csv_quote][realized_quotes]") {
    FileGuard cleanup("./tests/data/tmp_realized_quotes.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "id,quote_a,plain,quote_b\n"
            << "1,\"a\"\"b\",\"plain quoted\",\"x\"\"y\"\"z\"\n"
            << "2,\"left\"\"right\",\"still plain\",\"tail\"\"quote\"\n";
    }

    auto validate_reader = [&](CSVReader& reader) {
        CSVRow row;

        REQUIRE(reader.read_row(row));
        REQUIRE(row["quote_a"].get<std::string>() == "a\"b");
        REQUIRE(row["plain"].get<std::string>() == "plain quoted");
        REQUIRE(row["quote_b"].get<std::string>() == "x\"y\"z");

        const auto first_raw = row.raw_str();
        const auto first_realized = row["quote_a"].get<csv::string_view>();
        const auto first_plain = row["plain"].get<csv::string_view>();
        REQUIRE_FALSE(points_into(first_realized, first_raw));
        REQUIRE(points_into(first_plain, first_raw));

        REQUIRE(reader.read_row(row));
        REQUIRE(row["quote_a"].get<std::string>() == "left\"right");
        REQUIRE(row["plain"].get<std::string>() == "still plain");
        REQUIRE(row["quote_b"].get<std::string>() == "tail\"quote");

        REQUIRE_FALSE(reader.read_row(row));
    };

    SECTION("Memory-mapped file path") {
        CSVReader reader(cleanup.filename, CSVFormat());
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, CSVFormat());
        validate_reader(reader);
    }
}

TEST_CASE("Whitespace trimming applies after doubled quote realization", "[read_csv_trim][realized_quotes]") {
    FileGuard cleanup("./tests/data/tmp_realized_quotes_trim.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "id,text,tail\n"
            << "1,\"  a\"\"b  \",x\n"
            << "2,\" \t c\"\"d \t \",y\n";
    }

    CSVFormat format;
    format.header_row(0)
        .trim({ ' ', '\t' })
        .delimiter(',');

    auto validate_reader = [&](CSVReader& reader) {
        CSVRow row;
        REQUIRE(reader.read_row(row));
        REQUIRE(row["text"].get<std::string>() == "a\"b");
        REQUIRE(row["tail"].get<std::string>() == "x");

        REQUIRE(reader.read_row(row));
        REQUIRE(row["text"].get<std::string>() == "c\"d");
        REQUIRE(row["tail"].get<std::string>() == "y");

        REQUIRE_FALSE(reader.read_row(row));
    };

    SECTION("Memory-mapped file path") {
        CSVReader reader(cleanup.filename, format);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, format);
        validate_reader(reader);
    }
}

TEST_CASE("Doubled quotes across chunks are realized for mmap and stream readers", "[read_csv_quote][realized_quotes][chunk_boundary]") {
    FileGuard cleanup("./tests/data/tmp_realized_quotes_chunks.csv");
    std::vector<std::string> expected;
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "id,text,tail\n";

        size_t row = 0;
        while (expected.size() < 3 || static_cast<size_t>(out.tellp()) < internals::CSV_CHUNK_SIZE_FLOOR + 4096) {
            const std::string value = "row-" + std::to_string(row) + "-\"quoted\"";
            out << row << ",\"row-" << row << "-\"\"quoted\"\"\",tail-" << row << "\n";
            expected.push_back(value);
            row++;
        }
    }

    auto validate_reader = [&](CSVReader& reader) {
        CSVRow row;
        for (size_t i = 0; i < expected.size(); ++i) {
            REQUIRE(reader.read_row(row));
            REQUIRE(row["id"].get<size_t>() == i);
            REQUIRE(row["text"].get<std::string>() == expected[i]);
            REQUIRE(row["tail"].get<std::string>() == "tail-" + std::to_string(i));
        }

        REQUIRE_FALSE(reader.read_row(row));
    };

    SECTION("Memory-mapped file path") {
        CSVFormat format;
        format.chunk_size(internals::CSV_CHUNK_SIZE_FLOOR);

        CSVReader reader(cleanup.filename, format);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        CSVFormat format;
        format.chunk_size(internals::CSV_CHUNK_SIZE_FLOOR);

        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, format);
        validate_reader(reader);
    }
}

TEST_CASE("CSVReader::read_chunk consumes rows in bounded batches", "[read_chunk]") {
    FileGuard cleanup("./tests/data/tmp_read_chunk.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "id,name,value\n"
            << "1,Alice,10\n"
            << "2,Bob,20\n"
            << "3,Carol,30\n"
            << "4,Dave,40\n"
            << "5,Eve,50\n";
    }

    auto validate_reader = [&](CSVReader& reader) {
        //! [CSVReader read_chunk Example]
        std::vector<CSVRow> chunk;

        REQUIRE(reader.read_chunk(chunk, 2));
        REQUIRE(chunk.size() == 2);
        REQUIRE(chunk[0]["id"].get<std::string>() == "1");
        REQUIRE(chunk[0]["name"].get<std::string>() == "Alice");
        REQUIRE(chunk[0]["value"].get<std::string>() == "10");
        REQUIRE(chunk[1]["id"].get<std::string>() == "2");
        REQUIRE(chunk[1]["name"].get<std::string>() == "Bob");
        REQUIRE(chunk[1]["value"].get<std::string>() == "20");

        REQUIRE(reader.read_chunk(chunk, 2));
        REQUIRE(chunk.size() == 2);
        REQUIRE(chunk[0]["id"].get<std::string>() == "3");
        REQUIRE(chunk[0]["name"].get<std::string>() == "Carol");
        REQUIRE(chunk[0]["value"].get<std::string>() == "30");
        REQUIRE(chunk[1]["id"].get<std::string>() == "4");
        REQUIRE(chunk[1]["name"].get<std::string>() == "Dave");
        REQUIRE(chunk[1]["value"].get<std::string>() == "40");

        REQUIRE(reader.read_chunk(chunk, 2));
        REQUIRE(chunk.size() == 1);
        REQUIRE(chunk[0]["id"].get<std::string>() == "5");
        REQUIRE(chunk[0]["name"].get<std::string>() == "Eve");
        REQUIRE(chunk[0]["value"].get<std::string>() == "50");

        REQUIRE_FALSE(reader.read_chunk(chunk, 2));
        REQUIRE(chunk.empty());
        REQUIRE_FALSE(reader.read_chunk(chunk, 2));
        REQUIRE(chunk.empty());
        //! [CSVReader read_chunk Example]
    };

    SECTION("Memory-mapped file path") {
        CSVReader reader(cleanup.filename);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, CSVFormat());
        validate_reader(reader);
    }
}

TEST_CASE("CSVReader::read_chunk with zero max rows does not consume data", "[read_chunk]") {
    FileGuard cleanup("./tests/data/tmp_read_chunk_zero.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "id,name,value\n"
            << "1,Alice,10\n"
            << "2,Bob,20\n";
    }

    auto validate_reader = [&](CSVReader& reader) {
        std::vector<CSVRow> chunk(1);

        REQUIRE_FALSE(reader.read_chunk(chunk, 0));
        REQUIRE(chunk.empty());

        REQUIRE(reader.read_chunk(chunk, 1));
        REQUIRE(chunk.size() == 1);
        REQUIRE(chunk[0]["id"].get<std::string>() == "1");
        REQUIRE(chunk[0]["name"].get<std::string>() == "Alice");

        CSVRow row;
        REQUIRE(reader.read_row(row));
        REQUIRE(row["id"].get<std::string>() == "2");
        REQUIRE(row["name"].get<std::string>() == "Bob");
        REQUIRE_FALSE(reader.read_row(row));
    };

    SECTION("Memory-mapped file path") {
        CSVReader reader(cleanup.filename);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, CSVFormat());
        validate_reader(reader);
    }
}

TEST_CASE("CSVReader move operations preserve unread rows", "[csv_reader][move]") {
    FileGuard cleanup("./tests/data/tmp_reader_move.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "id,name,value\n"
            << "1,Alice,10\n"
            << "2,Bob,20\n"
            << "3,Carol,30\n";
    }

    auto validate_reader = [](CSVReader& reader) {
        CSVRow row;

        REQUIRE(reader.read_row(row));
        REQUIRE(row["id"].get<std::string>() == "2");
        REQUIRE(row["name"].get<std::string>() == "Bob");

        REQUIRE(reader.read_row(row));
        REQUIRE(row["id"].get<std::string>() == "3");
        REQUIRE(row["name"].get<std::string>() == "Carol");

        REQUIRE_FALSE(reader.read_row(row));
    };

    SECTION("Move construction from memory-mapped file path") {
        CSVReader original(cleanup.filename);

        CSVRow row;
        REQUIRE(original.read_row(row));
        REQUIRE(row["id"].get<std::string>() == "1");

        CSVReader moved(std::move(original));
        validate_reader(moved);
    }

    SECTION("Move construction from std::istream path") {
        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader original(infile, CSVFormat());

        CSVRow row;
        REQUIRE(original.read_row(row));
        REQUIRE(row["id"].get<std::string>() == "1");

        CSVReader moved(std::move(original));
        validate_reader(moved);
    }

    SECTION("Move assignment from memory-mapped file path") {
        CSVReader original(cleanup.filename);
        CSVReader moved("./tests/data/fake_data/ints.csv");

        CSVRow row;
        REQUIRE(original.read_row(row));
        REQUIRE(row["id"].get<std::string>() == "1");

        moved = std::move(original);
        validate_reader(moved);
    }

    SECTION("Move assignment from std::istream path") {
        std::ifstream infile(cleanup.filename, std::ios::binary);
        std::stringstream placeholder("A\nplaceholder\n");
        CSVReader original(infile, CSVFormat());
        CSVReader moved(placeholder, CSVFormat());

        CSVRow row;
        REQUIRE(original.read_row(row));
        REQUIRE(row["id"].get<std::string>() == "1");

        moved = std::move(original);
        validate_reader(moved);
    }
}

TEST_CASE("CSVReader no_quote treats quote bytes as ordinary data", "[csv_format][no_quote]") {
    FileGuard cleanup("./tests/data/tmp_no_quote_literal_quotes.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "id,text,tail\n"
            << "1,\"alpha,beta\"\n"
            << "2,gamma,\"delta\"\n";
    }

    auto validate_reader = [&](CSVReader& reader) {
        REQUIRE_FALSE(reader.get_format().is_quoting_enabled());

        CSVRow row;
        REQUIRE(reader.read_row(row));
        REQUIRE(row["id"].get<std::string>() == "1");
        REQUIRE(row["text"].get<std::string>() == "\"alpha");
        REQUIRE(row["tail"].get<std::string>() == "beta\"");

        REQUIRE(reader.read_row(row));
        REQUIRE(row["id"].get<std::string>() == "2");
        REQUIRE(row["text"].get<std::string>() == "gamma");
        REQUIRE(row["tail"].get<std::string>() == "\"delta\"");

        REQUIRE_FALSE(reader.read_row(row));
    };

    SECTION("Memory-mapped file path") {
        CSVFormat format;
        format.quote(false);

        CSVReader reader(cleanup.filename, format);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        CSVFormat format;
        format.quote(false);

        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, format);
        validate_reader(reader);
    }
}

#if CSV_ENABLE_THREADS
TEST_CASE("CSVReader speculative read_chunk preserves format flags", "[read_chunk][speculative]") {
    const std::string& filename = speculative_read_chunk_flags_filename();

    CSVFormat format;
    format.delimiter(',')
        .trim({ ' ' })
        .quote(false)
        .variable_columns(VariableColumnPolicy::IGNORE_ROW)
        .chunk_size(internals::CSV_CHUNK_SIZE_FLOOR)
        .speculative_parallel_min_bytes(1)
        .speculative_parallel_threads(2);

    auto validate_reader = [&](CSVReader& reader) {
        REQUIRE(reader.parse_worker_count() == 2);
        REQUIRE_FALSE(reader.get_format().is_quoting_enabled());

        std::vector<CSVRow> chunk;
        size_t rows_seen = 0;
        while (reader.read_chunk(chunk, 777)) {
            REQUIRE(chunk.size() <= 777);
            for (auto& row : chunk) {
                const size_t id = row["id"].get<size_t>();
                REQUIRE(id == rows_seen);
                REQUIRE(row["text"].get<std::string>() == "\"literal_" + internals::to_string(id) + "\"");
                REQUIRE(row["tail"].get<std::string>() == "tail_" + internals::to_string(id));
                rows_seen++;
            }
        }

        REQUIRE(rows_seen == 100000);
        REQUIRE(reader.n_rows() == 100000);
        REQUIRE(reader.speculative_diagnostics().chunks > 0);
    };

    SECTION("Memory-mapped file path") {
        CSVReader reader(filename, format);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        std::ifstream infile(filename, std::ios::binary);
        CSVReader reader(infile, format);
        validate_reader(reader);
    }
}
#endif

TEST_CASE("Issue #195 - header_row() preserved when delimiter guessing", "[issue_195][skip_rows_file]") {
    // When the user explicitly sets header_row(N) alongside guess_csv(),
    // the guesser must not override the user's chosen header row.
    FileGuard cleanup("./tests/data/tmp_issue_195_skip_rows.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "a;b;c;d\n"
            << "this;is;before;header\n"
            << "this;is;before;header_too\n"
            << "timestamp;distance;angle;amplitude\n"
            << "22857782;30000;314159;0\n"
            << "22857786;30000;314109;0\n";
    }

    std::vector<std::string> expected = { "timestamp", "distance", "angle", "amplitude" };

    auto validate_reader = [&](CSVReader& reader) {
        REQUIRE(reader.get_col_names() == expected);

        // Verify data rows are also correct
        std::vector<CSVRow> rows;
        for (auto& row : reader) rows.push_back(row);
        REQUIRE(rows.size() == 2);
        REQUIRE(rows[0]["timestamp"].get<int>() == 22857782);
    };

    SECTION("Memory-mapped file path") {
        auto format = CSVFormat::guess_csv();
        format.header_row(3);

        CSVReader reader(cleanup.filename, format);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        auto format = CSVFormat::guess_csv();
        format.header_row(3);

        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, format);
        validate_reader(reader);
    }
}

TEST_CASE("Header inference with explicit delimiter", "[header_infer_explicit_delim]") {
    // Even when delimiter guessing is disabled (single explicit delimiter),
    // header and mode-width inference should still run unless header is
    // explicitly user-set.
    FileGuard cleanup("./tests/data/tmp_header_infer_explicit_delim.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "comment\n"
            << "metadata\n"
            << "A;B;C\n"
            << "1;2;3\n"
            << "4;5;6\n";
    }

    auto validate_reader = [&](CSVReader& reader) {
        REQUIRE(reader.get_col_names() == std::vector<std::string>({ "A", "B", "C" }));

        std::vector<CSVRow> rows;
        for (auto& row : reader) rows.push_back(row);

        REQUIRE(rows.size() == 2);
        REQUIRE(rows[0]["A"].get<int>() == 1);
        REQUIRE(rows[0]["B"].get<int>() == 2);
        REQUIRE(rows[0]["C"].get<int>() == 3);
        REQUIRE(rows[1]["A"].get<int>() == 4);
        REQUIRE(rows[1]["B"].get<int>() == 5);
        REQUIRE(rows[1]["C"].get<int>() == 6);
    };

    SECTION("Memory-mapped file path") {
        CSVFormat format;
        format.delimiter(';');

        CSVReader reader(cleanup.filename, format);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        CSVFormat format;
        format.delimiter(';');

        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, format);
        validate_reader(reader);
    }
}

TEST_CASE("No-header n_cols inference with explicit delimiter", "[no_header_ncols_explicit_delim]") {
    // With an explicit delimiter and no_header(), n_cols should still be inferred
    // so variable-column policies can behave correctly.
    FileGuard cleanup("./tests/data/tmp_no_header_ncols_explicit_delim.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << "junk\n"
            << "meta\n"
            << "1;2;3\n"
            << "4;5;6\n";
    }

    auto validate_reader = [&](CSVReader& reader) {
        std::vector<CSVRow> rows;
        for (auto& row : reader) rows.push_back(row);

        // Mode width is 3, so short rows should be dropped by IGNORE_ROW.
        REQUIRE(rows.size() == 2);
        REQUIRE(rows[0].size() == 3);
        REQUIRE(rows[0][0].get<int>() == 1);
        REQUIRE(rows[0][1].get<int>() == 2);
        REQUIRE(rows[0][2].get<int>() == 3);
        REQUIRE(rows[1].size() == 3);
        REQUIRE(rows[1][0].get<int>() == 4);
        REQUIRE(rows[1][1].get<int>() == 5);
        REQUIRE(rows[1][2].get<int>() == 6);
    };

    SECTION("Memory-mapped file path") {
        CSVFormat format;
        format.delimiter(';').no_header().variable_columns(VariableColumnPolicy::IGNORE_ROW);

        CSVReader reader(cleanup.filename, format);
        validate_reader(reader);
    }

    SECTION("std::istream path") {
        CSVFormat format;
        format.delimiter(';').no_header().variable_columns(VariableColumnPolicy::IGNORE_ROW);

        std::ifstream infile(cleanup.filename, std::ios::binary);
        CSVReader reader(infile, format);
        validate_reader(reader);
    }
}
#endif
