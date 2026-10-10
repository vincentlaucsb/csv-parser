/** @file
 *  Tests for CSV parsing
 */

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
    const std::string UTF8_BOM = "\xEF\xBB\xBF";

    void write_binary_file(const std::string& filename, const std::string& data) {
        std::ofstream out(filename, std::ios::binary);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    /** Run `validate` against the same bytes through the mmap and stream constructors. */
    template<typename Validate>
    void validate_both_paths(
        const std::string& data,
        const CSVFormat& format,
        const std::string& filename,
        Validate validate
    ) {
        SECTION("mmap path") {
            FileGuard cleanup(filename);
            write_binary_file(filename, data);
            CSVReader reader(filename, format);
            validate(reader);
        }

        SECTION("stream path") {
            std::istringstream input(data);
            CSVReader reader(input, format);
            validate(reader);
        }
    }

    void require_encoding_rejected_both_paths(
        const std::string& data,
        const CSVFormat& format,
        const std::string& filename,
        const std::string& encoding
    ) {
        SECTION("mmap path") {
            FileGuard cleanup(filename);
            write_binary_file(filename, data);
            REQUIRE_THROWS_WITH(CSVReader(filename, format), Catch::Matchers::ContainsSubstring(encoding));
        }

        SECTION("stream path") {
            std::istringstream input(data);
            REQUIRE_THROWS_WITH(CSVReader(input, format), Catch::Matchers::ContainsSubstring(encoding));
        }
    }

    const std::string& large_utf8_bom_filename() {
        static csv_test::GeneratedFile file("./tests/data/tmp_large_utf8_bom.csv");

        return file.path([](std::ofstream& out) {
            out << UTF8_BOM << "A,B,C\n";
            for (size_t i = 0; i < 600000; ++i) {
                out << i * 3 << ',' << i * 3 + 1 << ',' << i * 3 + 2 << '\n';
            }
        });
    }
}

TEST_CASE( "Test Parse Flags", "[test_parse_flags]" ) {
    REQUIRE(internals::make_parse_flags(',', '"')[162] == internals::ParseFlags::QUOTE);
}

// Test Main Functions
TEST_CASE("Test Reading CSV From Direct Input", "[read_csv_direct]" ) {
    SECTION("Expected Results") {
        auto rows = "A,B,C\r\n" // Header row
            "123,234,345\r\n"
            "1,2,3\r\n"
            "4,5,6"_csv;

        CSVRow row;
        rows.read_row(row);
        vector<string> first_row = { "123", "234", "345" };
        REQUIRE(vector<string>(row) == first_row);
        
        rows.read_row(row);
        vector<string> second_row = { "1", "2", "3" };
        REQUIRE(vector<string>(row) == second_row);

        rows.read_row(row);
        vector<string> third_row = { "4", "5", "6" };
        REQUIRE(vector<string>(row) == third_row);

        REQUIRE(rows.n_rows() == 3);
    }

    SECTION("Expected Results: No Header") {
        auto rows = "123,234,345\r\n"
            "1,2,3\r\n"
            "1,2,3"_csv_no_header;

        CSVRow row;
        rows.read_row(row);
        vector<string> first_row = { "123", "234", "345" };
        REQUIRE(vector<string>(row) == first_row);
    }
}

TEST_CASE("Assert UTF-8 Handling Works", "[read_utf8_direct]") {
    auto rows = "\xEF\xBB\xBF"  // BOM
        "A,B,C\r\n"             // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3"_csv;

    // Flag should be set
    REQUIRE(rows.utf8_bom());
    REQUIRE(rows.get_col_names() == std::vector<std::string>({ "A", "B", "C" }));

    CSVRow row;
    rows.read_row(row);
    vector<string> first_row = { "123", "234", "345" };
    REQUIRE(vector<string>(row) == first_row);
}

TEST_CASE("Unicode BOM handling", "[read_unicode_bom]") {
    SECTION("Free utility strips UTF-8 BOM") {
        bool utf8_bom = false;
        REQUIRE(internals::get_bom_skip_or_throw(csv::string_view("\xEF\xBB\xBF" "A,B\n", 7), utf8_bom) == 3);
        REQUIRE(utf8_bom);
    }

    SECTION("Free utility rejects UTF-16 and UTF-32 BOMs") {
        bool utf8_bom = false;
        REQUIRE_THROWS_WITH(
            internals::get_bom_skip_or_throw(csv::string_view("\xFF\xFE" "A\0", 4), utf8_bom),
            Catch::Matchers::ContainsSubstring("UTF-16")
        );
        REQUIRE_THROWS_WITH(
            internals::get_bom_skip_or_throw(csv::string_view("\x00\x00\xFE\xFF", 4), utf8_bom),
            Catch::Matchers::ContainsSubstring("UTF-32")
        );
    }

    SECTION("Direct input rejects UTF-16 before parsing corrupted rows") {
        REQUIRE_THROWS_WITH([] {
            return csv::parse_unsafe(csv::string_view("\xFF\xFE" "A\0,\0B\0\n\0", 10));
        }(), Catch::Matchers::ContainsSubstring("UTF-16"));
    }

    SECTION("Stream input rejects UTF-16 before parsing corrupted rows") {
        std::string data("\xFE\xFF\0A\0,\0B\0\n", 10);
        std::istringstream input(data);

        REQUIRE_THROWS_WITH(CSVReader(input), Catch::Matchers::ContainsSubstring("UTF-16"));
    }

    SECTION("File input rejects UTF-16 before parsing corrupted rows") {
        const std::string filename = "./tests/data/tmp_utf16_bom.csv";
        FileGuard cleanup(filename);
        {
            std::ofstream out(filename, std::ios::binary);
            const std::string data("\xFF\xFE" "A\0,\0B\0\n\0", 10);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }

        REQUIRE_THROWS_WITH(CSVReader(filename), Catch::Matchers::ContainsSubstring("UTF-16"));
    }
}

TEST_CASE("UTF-8 BOM is stripped on both parser paths", "[read_unicode_bom]") {
    CSVFormat guessed = CSVFormat::guess_csv();
    CSVFormat explicit_format;
    explicit_format.delimiter(',').header_row(0);

    auto validate_rows = [](CSVReader& reader) {
        REQUIRE(reader.utf8_bom());
        REQUIRE(reader.get_col_names() == vector<string>({ "A", "B", "C" }));

        vector<vector<string>> rows;
        for (auto& row : reader) {
            rows.push_back(vector<string>(row));
        }
        REQUIRE(rows == vector<vector<string>>({ { "1", "2", "3" }, { "4", "5", "6" } }));
    };

    const std::string data = UTF8_BOM + "A,B,C\n1,2,3\n4,5,6\n";

    SECTION("Guessed format") {
        validate_both_paths(data, guessed, "./tests/data/tmp_utf8_bom_guessed.csv", validate_rows);
    }

    SECTION("Explicit format skips guessing") {
        validate_both_paths(data, explicit_format, "./tests/data/tmp_utf8_bom_explicit.csv", validate_rows);
    }

    SECTION("Only one leading BOM is stripped") {
        const std::string double_bom = UTF8_BOM + UTF8_BOM + "A,B\n1,2\n";
        validate_both_paths(double_bom, explicit_format, "./tests/data/tmp_utf8_double_bom.csv", [](CSVReader& reader) {
            REQUIRE(reader.utf8_bom());
            REQUIRE(reader.get_col_names() == vector<string>({ UTF8_BOM + "A", "B" }));
        });
    }
}

TEST_CASE("UTF-16 and UTF-32 BOMs are rejected on both parser paths", "[read_unicode_bom]") {
    CSVFormat guessed = CSVFormat::guess_csv();
    CSVFormat explicit_format;
    explicit_format.delimiter(',').header_row(0);

    const std::string utf16_le("\xFF\xFE" "A\0,\0B\0\n\0", 10);
    const std::string utf16_be("\xFE\xFF" "\0A\0,\0B\0\n", 10);
    const std::string utf32_le("\xFF\xFE\0\0" "A\0\0\0,\0\0\0B\0\0\0\n\0\0\0", 20);
    const std::string utf32_be("\0\0\xFE\xFF" "\0\0\0A\0\0\0,\0\0\0B\0\0\0\n", 20);
    const std::string filename = "./tests/data/tmp_rejected_unicode_bom.csv";

    SECTION("UTF-16 LE, guessed format") {
        require_encoding_rejected_both_paths(utf16_le, guessed, filename, "UTF-16");
    }

    SECTION("UTF-16 BE, explicit format") {
        require_encoding_rejected_both_paths(utf16_be, explicit_format, filename, "UTF-16");
    }

    SECTION("UTF-16 LE, explicit format") {
        require_encoding_rejected_both_paths(utf16_le, explicit_format, filename, "UTF-16");
    }

    SECTION("UTF-32 LE is not mistaken for UTF-16 LE") {
        require_encoding_rejected_both_paths(utf32_le, guessed, filename, "UTF-32");
    }

    SECTION("UTF-32 BE, explicit format") {
        require_encoding_rejected_both_paths(utf32_be, explicit_format, filename, "UTF-32");
    }
}

TEST_CASE("Tiny UTF-8 BOM inputs behave consistently", "[read_unicode_bom]") {
    CSVFormat format;
    format.delimiter(',').no_header();

    SECTION("Empty input") {
        // Stream only: a zero-byte file opened by filename is reported as an
        // open failure (see "Empty CSV does not crash parser entry points").
        std::istringstream input("");
        CSVReader reader(input, format);
        size_t n_rows = 0;
        for (auto& row : reader) { (void)row; ++n_rows; }
        REQUIRE(n_rows == 0);
        REQUIRE_FALSE(reader.utf8_bom());
    }

    SECTION("BOM-only file") {
        validate_both_paths(UTF8_BOM, format, "./tests/data/tmp_bom_only.csv", [](CSVReader& reader) {
            size_t n_rows = 0;
            for (auto& row : reader) { (void)row; ++n_rows; }
            REQUIRE(n_rows == 0);
            REQUIRE(reader.utf8_bom());
        });
    }

    SECTION("BOM followed by one row without a trailing newline") {
        validate_both_paths(UTF8_BOM + "1,2,3", format, "./tests/data/tmp_bom_one_row.csv", [](CSVReader& reader) {
            vector<vector<string>> rows;
            for (auto& row : reader) {
                rows.push_back(vector<string>(row));
            }
            REQUIRE(rows == vector<vector<string>>({ { "1", "2", "3" } }));
            REQUIRE(reader.utf8_bom());
        });
    }
}

TEST_CASE("UTF-8 BOM is not part of the first row", "[read_unicode_bom]") {
    // The BOM precedes the first row rather than belonging to it, so the
    // row's raw text and source offset both start after the three BOM bytes.
    CSVFormat format;
    format.delimiter(',').no_header();

    const std::string data = UTF8_BOM + "1,2,3\n4,5,6\n";
    validate_both_paths(data, format, "./tests/data/tmp_bom_first_row.csv", [](CSVReader& reader) {
        CSVRow row;
        REQUIRE(reader.read_row(row));
        REQUIRE(row.raw_str() == "1,2,3");
        REQUIRE(row.byte_offset() == 3);
        REQUIRE(row[0] == "1");

        REQUIRE(reader.read_row(row));
        REQUIRE(row.raw_str() == "4,5,6");
        REQUIRE(row.byte_offset() == 9);
    });
}

TEST_CASE("UTF-8 BOM with a file crossing chunk boundaries", "[read_unicode_bom]") {
    const std::string& filename = large_utf8_bom_filename();

    auto validate_reader = [](CSVReader& reader) {
        REQUIRE(reader.utf8_bom());
        REQUIRE(reader.get_col_names() == vector<string>({ "A", "B", "C" }));

        size_t i = 0;
        for (auto& row : reader) {
            REQUIRE(row.size() == 3);
            REQUIRE(row[0].get<size_t>() == i * 3);
            REQUIRE(row[1].get<size_t>() == i * 3 + 1);
            REQUIRE(row[2].get<size_t>() == i * 3 + 2);
            ++i;
        }
        REQUIRE(i == 600000);
    };

    CSVFormat serial;
    serial.delimiter(',').header_row(0).speculative_parallel_threads(1);

    // Small chunks make the first stream window larger than one serial chunk,
    // which sends it through the speculative parser.
    CSVFormat speculative;
    speculative.delimiter(',').header_row(0)
        .chunk_size(internals::CSV_CHUNK_SIZE_FLOOR)
        .speculative_parallel_threads(4)
        .speculative_parallel_min_bytes(0);

    auto check_worker_count = [](CSVReader& reader, size_t expected) {
#if CSV_ENABLE_THREADS
        REQUIRE(reader.parse_worker_count() == expected);
#else
        (void)reader;
        (void)expected;
#endif
    };

    SECTION("Serial, mmap path") {
        CSVReader reader(filename, serial);
        validate_reader(reader);
    }

    SECTION("Serial, stream path") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, serial);
        validate_reader(reader);
    }

    SECTION("Speculative, mmap path") {
        CSVReader reader(filename, speculative);
        check_worker_count(reader, 4);
        validate_reader(reader);
    }

    SECTION("Speculative, stream path") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, speculative);
        check_worker_count(reader, 4);
        validate_reader(reader);
    }
}

#ifndef __EMSCRIPTEN__
TEST_CASE("guess_format() sees through a UTF-8 BOM", "[read_unicode_bom]") {
    SECTION("UTF-8 BOM") {
        const std::string filename = "./tests/data/tmp_guess_format_bom.csv";
        FileGuard cleanup(filename);
        write_binary_file(filename, UTF8_BOM + "A|B|C\n1|2|3\n4|5|6\n");

        const auto guessed = guess_format(filename);
        REQUIRE(guessed.delim == '|');
        REQUIRE(guessed.header_row == 0);
        REQUIRE(guessed.n_cols == 3);
    }

    SECTION("UTF-16 is rejected") {
        const std::string filename = "./tests/data/tmp_guess_format_utf16.csv";
        FileGuard cleanup(filename);
        write_binary_file(filename, std::string("\xFF\xFE" "A\0,\0B\0\n\0", 10));

        REQUIRE_THROWS_WITH(guess_format(filename), Catch::Matchers::ContainsSubstring("UTF-16"));
    }
}
#endif

//! [Escaped Comma]
TEST_CASE( "Test Escaped Comma", "[read_csv_comma]" ) {
    auto rows = "A,B,C\r\n" // Header row
                "123,\"234,345\",456\r\n"
                "1,2,3\r\n"
                "1,2,3"_csv;

    CSVRow row;
    rows.read_row(row);
    REQUIRE( vector<string>(row) == 
        vector<string>({"123", "234,345", "456"}));
}
//! [Escaped Comma]

TEST_CASE( "Test Escaped Newline", "[read_csv_newline]" ) {
    auto rows = "A,B,C\r\n" // Header row
                "123,\"234\n,345\",456\r\n"
                "1,2,3\r\n"
                "1,2,3"_csv;

    CSVRow row;
    rows.read_row(row);
    REQUIRE( vector<string>(row) == 
        vector<string>({ "123", "234\n,345", "456" }) );
}

TEST_CASE("Test Escaped Newline & Empty Last Column", "[read_csv_empty_last_column]") {
    auto rows = "A,B,C,\r\n" // Header row
        "123,\"234\n,345\",456,\"\"\r\n"
        "1,2,3,\r\n"
        "4,5,6,\"\""_csv;

    CSVRow row;
    rows.read_row(row);
    REQUIRE(vector<string>(row) ==
        vector<string>({ "123", "234\n,345", "456", "" }));

    rows.read_row(row);
    REQUIRE(vector<string>(row) ==
        vector<string>({ "1", "2", "3", "" }));

    rows.read_row(row);
    REQUIRE(vector<string>(row) ==
        vector<string>({ "4", "5", "6", ""}));
}

TEST_CASE("Test Unquoted Trailing Empty Field", "[read_csv_empty_last_column]") {
    auto rows = "A,B,C,D\r\n" // Header row
        "1,2,3,\r\n"
        "4,5,6,"_csv;

    CSVRow row;
    rows.read_row(row);
    REQUIRE(vector<string>(row) ==
        vector<string>({ "1", "2", "3", "" }));

    rows.read_row(row);
    REQUIRE(vector<string>(row) ==
        vector<string>({ "4", "5", "6", "" }));
}

TEST_CASE("Test Quoted Empty Single-Column Row", "[read_csv_empty_last_column]") {
    auto rows = "A\r\n" // Header row
        "\"\"\r\n"
        "value"_csv;

    CSVRow row;
    rows.read_row(row);
    REQUIRE(vector<string>(row) ==
        vector<string>({ "" }));

    rows.read_row(row);
    REQUIRE(vector<string>(row) ==
        vector<string>({ "value" }));
}

TEST_CASE( "Test Empty Field", "[read_empty_field]" ) {
    // Per RFC 1480, escaped quotes should be doubled up
    auto rows = "A,B,C\r\n" // Header row
                "123,\"\",456\r\n"_csv;

    CSVRow row;
    rows.read_row(row);
    REQUIRE( vector<string>(row) == 
        vector<string>({ "123", "", "456" }) );
}

//! [Parse Example]
TEST_CASE( "Test Escaped Quote", "[read_csv_quote]" ) {
    // Per RFC 1480, escaped quotes should be doubled up
    auto csv_string = GENERATE(as<std::string> {}, 
        (
            "A,B,C\r\n" // Header row
            "123,\"234\"\"345\",456\r\n"
            "123,\"234\"345\",456\r\n"  // Unescaped single quote (not strictly valid)
            "123,\"234\"345\",\"456\"" // Quoted field at the end
            "123, \"234\"345\",\"456\"" // Quoted field w/ leading whitespace
        ),
        (
            "\"A\",\"B\",\"C\"\r\n" // Header row
            "123,\"234\"\"345\",456\r\n"
            "123,\"234\"345\",456\r\n" // Unescaped single quote (not strictly valid)
            "123,\"234\"345\",\"456\"" // Quoted field at the end
            "123,\"234\"345\",\"456\"" // Quoted field w/ leading whitespace
        )
    );
    
    SECTION("Escaped Quote") {
        auto rows = parse(csv_string);

        REQUIRE(rows.get_col_names() == vector<string>({ "A", "B", "C" }));

        // Expected Results: Double " is an escape for a single "
        vector<string> correct_row = { "123", "234\"345", "456" };
        for (auto& row : rows) {
            REQUIRE(vector<string>(row) == correct_row);
        }
    }
}
//! [Parse Example]

//! [Parse Example]
TEST_CASE( "Test leading and trailing escaped quote", "[read_csv_quote]" ) {
    // Per RFC 4180, escaped quotes should be doubled up
    auto csv_string = GENERATE(as<std::string> {},
        (
            "A,B,C\r\n" // Header row
            "123,345,\"\"\"234\"\"\""
        )
    );
    
    SECTION("Double escaped Quote") {
        auto rows = parse(csv_string);

        REQUIRE(rows.get_col_names() == vector<string>({ "A", "B", "C" }));

        // Expected Results: Double quotes
        vector<string> correct_row = { "123", "345", "\"234\"" };
        for (auto& row : rows) {
            REQUIRE(vector<string>(row) == correct_row);
        }
    }
}
//! [Parse Example]

TEST_CASE("Normal Newlines", "[read_csv_normal_newline]") {
    auto row_str = GENERATE(as<std::string> {},
        // Windows style
        "A,B,C\r\n" // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "4,5,6",

        // Unix style
        "A,B,C\n" // Header row
        "123,234,345\n"
        "1,2,3\n"
        "4,5,6",

        // Old Mac Style
        "A,B,C\r" // Header row
        "123,234,345\r"
        "1,2,3\r"
        "4,5,6"
    );

    CSVFormat format;
    format.header_row(0).variable_columns(VariableColumnPolicy::KEEP);
    auto rows = parse(row_str, format);

    CSVRow row;
    rows.read_row(row);
    vector<string> first_row = { "123", "234", "345" };
    REQUIRE(vector<string>(row) == first_row);
    REQUIRE(row["A"] == "123");
    REQUIRE(row["B"] == "234");
    REQUIRE(row["C"] == "345");

    rows.read_row(row);
    vector<string> second_row = { "1", "2", "3" };
    REQUIRE(vector<string>(row) == second_row);

    rows.read_row(row);
    vector<string> third_row = { "4", "5", "6" };
    REQUIRE(vector<string>(row) == third_row);

    REQUIRE(rows.n_rows() == 3);
}


TEST_CASE("Edge-Case Newlines", "[read_csv_edge_case_newline]") {
    auto row_str = GENERATE(as<std::string> {},
        // Eww brother what is that...
        "A,B,C\r\r\n" // Header row
        "123,234,345\r\r\n"
        "1,2,3\r\r\n"
        "4,5,6",

        // Doubled-up Windows style
        "A,B,C\r\n\r\n" // Header row
        "123,234,345\r\n\r\n"
        "1,2,3\r\n\r\n"
        "4,5,6"
    );

    SECTION("KEEP policy - all rows including blanks") {
        CSVFormat format;
        format.header_row(0).variable_columns(VariableColumnPolicy::KEEP);
        auto rows = parse(row_str, format);

        CSVRow row;

        // Blank line from the trailing \r\n after the header
        rows.read_row(row);
        REQUIRE(row.empty());
        
        rows.read_row(row);
        vector<string> first_row = { "123", "234", "345" };
        REQUIRE(vector<string>(row) == first_row);
        REQUIRE(row["A"] == "123");
        REQUIRE(row["B"] == "234");
        REQUIRE(row["C"] == "345");

        // Blank line
        rows.read_row(row);
        REQUIRE(row.empty());

        rows.read_row(row);
        vector<string> second_row = { "1", "2", "3" };
        REQUIRE(vector<string>(row) == second_row);

        // Blank line
        rows.read_row(row);
        REQUIRE(row.empty());

        rows.read_row(row);
        vector<string> third_row = { "4", "5", "6" };
        REQUIRE(vector<string>(row) == third_row);

        REQUIRE(rows.n_rows() == 6);
    }

    SECTION("KEEP_NON_EMPTY policy - skip blank rows") {
        CSVFormat format;
        format.header_row(0).variable_columns(VariableColumnPolicy::KEEP_NON_EMPTY);
        auto rows = parse(row_str, format);

        CSVRow row;

        rows.read_row(row);
        vector<string> first_row = { "123", "234", "345" };
        REQUIRE(vector<string>(row) == first_row);
        REQUIRE(row["A"] == "123");
        REQUIRE(row["B"] == "234");
        REQUIRE(row["C"] == "345");

        rows.read_row(row);
        vector<string> second_row = { "1", "2", "3" };
        REQUIRE(vector<string>(row) == second_row);

        rows.read_row(row);
        vector<string> third_row = { "4", "5", "6" };
        REQUIRE(vector<string>(row) == third_row);

        REQUIRE(rows.n_rows() == 3);
    }
}

TEST_CASE("Test Whitespace Trimming", "[read_csv_trim]") {
    auto row_str = GENERATE(as<std::string> {},
        "A,B,C\r\n" // Header row
        "123,\"234\n,345\",456\r\n",

        // Random spaces
        "A,B,C\r\n"
        "   123,\"234\n,345\",    456\r\n",

        // Random spaces + tabs
        "A,B,C\r\n"
        "\t\t   123,\"234\n,345\",    456\r\n",

        // Spaces in quote escaped field
        "A,B,C\r\n"
        "\t\t   123,\"   234\n,345  \t\",    456\r\n",

        // Spaces in one header column
        "A,B,        C\r\n"
        "123,\"234\n,345\",456\r\n",

        // Random spaces + tabs in header
        "\t A,  B\t,     C\r\n"
        "123,\"234\n,345\",456\r\n",

        // Random spaces in header + data
        "A,B,        C\r\n"
        "123,\"234\n,345\",  456\r\n"
    );

    SECTION("Parse Test") {
        CSVFormat format;
        format.header_row(0)
            .trim({ '\t', ' ' })
            .delimiter(',');

        auto rows = parse(row_str, format);
        CSVRow row;
        rows.read_row(row);

        REQUIRE(vector<string>(row) ==
            vector<string>({ "123", "234\n,345", "456" }));
        REQUIRE(row["A"] == "123");
        REQUIRE(row["B"] == "234\n,345");
        REQUIRE(row["C"] == "456");
    }
}

inline std::vector<std::string> make_whitespace_test_cases() {
    std::vector<std::string> test_cases = {};
    std::stringstream ss;

    ss << "1, two,3" << std::endl
        << "4, ,5" << std::endl
        << " ,6, " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    // Lots of Whitespace
    ss << "1, two,3" << std::endl
        << "4,                    ,5" << std::endl
        << "         ,6,       " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    // Same as above but there's whitespace around 6
    ss << "1, two,3" << std::endl
        << "4,                    ,5" << std::endl
        << "         , 6 ,       " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    // Tabs
    ss << "1, two,3" << std::endl
        << "4, \t ,5" << std::endl
        << "\t\t\t\t\t ,6, \t " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    return test_cases;
}

TEST_CASE("Test Whitespace Trimming w/ Empty Fields") {
    auto csv_string = GENERATE(from_range(make_whitespace_test_cases()));

    SECTION("Parse Test") {
        CSVFormat format;
        format.column_names({ "A", "B", "C" })
            .trim({ ' ', '\t' });

        auto rows = parse(csv_string, format);
        CSVRow row;

        // First Row
        rows.read_row(row);
        REQUIRE(row[0].get<uint32_t>() == 1);
        REQUIRE(row[1].get<std::string>() == "two");
        REQUIRE(row[2].get<uint32_t>() == 3);

        // Second Row
        rows.read_row(row);
        REQUIRE(row[0].get<uint32_t>() == 4);
        REQUIRE(row[1].is_null());
        REQUIRE(row[2].get<uint32_t>() == 5);

        // Third Row
        rows.read_row(row);
        REQUIRE(row[0].is_null());
        REQUIRE(row[1].get<uint32_t>() == 6);
        REQUIRE(row[2].is_null());

        // Fourth Row
        rows.read_row(row);
        REQUIRE(row[0].get<uint32_t>() == 7);
        REQUIRE(row[1].get<uint32_t>() == 8);
        REQUIRE(row[2].get<uint32_t>() == 9);
    }
}

TEST_CASE("Test Variable Row Length Handling", "[read_csv_var_len]") {
    string csv_string("A,B,C\r\n" // Header row
        "123,234,345\r\n"
        "1,2,3\r\n"
        "6,9\r\n" // Short row
        "6,9,7,10\r\n" // Long row
        "1,2,3"),
        error_message = "";
    bool error_caught = false;

    SECTION("Throw Error") {
        CSVFormat format;
        format.variable_columns(VariableColumnPolicy::THROW);

        auto rows = parse(csv_string, format);
        size_t i = 0;

        try {
            for (auto it = rows.begin(); it != rows.end(); ++it) {
                i++;
            }
        }
        catch (std::runtime_error& err) {
            error_caught = true;
            error_message = err.what();
        }

        REQUIRE(error_caught);
        REQUIRE(i == 2);
        REQUIRE(error_message.find(internals::ERROR_ROW_TOO_SHORT) == 0);
    }

    SECTION("Ignore Row") {
        CSVFormat format;
        format.variable_columns(false);

        auto reader = parse(csv_string, format);
        std::vector<CSVRow> rows(reader.begin(), reader.end());

        // Expect short/long rows to be dropped
        REQUIRE(rows.size() == 3);
    }

    SECTION("Keep Row") {
        CSVFormat format;
        format.variable_columns(true);

        auto reader = parse(csv_string, format);
        std::vector<CSVRow> rows(reader.begin(), reader.end());

        // Expect short/long rows to be kept
        REQUIRE(rows.size() == 5);
        REQUIRE(rows[2][0] == 6);
        REQUIRE(rows[2][1] == 9);

        // Should be able to index extra columns via numeric index
        REQUIRE(rows[3][2] == 7);
        REQUIRE(rows[3][3] == 10);
    }
}

TEST_CASE("Test read_row() CSVField - Memory", "[read_row_csvf2]") {
    CSVFormat format;
    format.column_names({ "A", "B" });

    std::stringstream csv_string;
    csv_string << "3.14,9999" << std::endl
        << "60,70" << std::endl
        << "," << std::endl;

    auto rows = parse(csv_string.str(), format);
    CSVRow row;
    rows.read_row(row);

    // First Row
    REQUIRE((row[0].is_float() && row[0].is_num()));
    REQUIRE(row[0].get<std::string>().substr(0, 4) == "3.14");
    REQUIRE(internals::is_equal(row[0].get<double>(), 3.14));

    // Second Row
    rows.read_row(row);
    REQUIRE((row[0].is_int() && row[0].is_num()));
    REQUIRE((row[1].is_int() && row[1].is_num()));
    REQUIRE(row[0].get<std::string>() == "60");
    REQUIRE(row[1].get<std::string>() == "70");

    // Third Row
    rows.read_row(row);
    REQUIRE(row[0].is_null());
    REQUIRE(row[1].is_null());
}

// Reported in: https://github.com/vincentlaucsb/csv-parser/issues/56
TEST_CASE("Leading Empty Field Regression", "[empty_field_regression]") {
    std::stringstream csv_string(R"(category,subcategory,project name
,,foo-project
bar-category,,bar-project
	)");

    CSVReader reader(csv_string, CSVFormat());
    
    CSVRow first_row, second_row;
    REQUIRE(reader.read_row(first_row));
    REQUIRE(reader.read_row(second_row));

    REQUIRE(first_row["category"] == "");
    REQUIRE(first_row["subcategory"] == "");
    REQUIRE(first_row["project name"] == "foo-project");

    REQUIRE(second_row["category"] == "bar-category");
    REQUIRE(second_row["subcategory"] == "");
    REQUIRE(second_row["project name"] == "bar-project");
}

TEST_CASE("Test Parsing CSV with Dummy Column", "[read_csv_dummy]") {
    std::stringstream csv_string(R"(A,B,C,
123,345,678,)");

    CSVReader reader(csv_string, CSVFormat());

    CSVRow first_row;

    REQUIRE(reader.get_col_names() == std::vector<std::string>({"A","B","C",""}));

    reader.read_row(first_row);
    REQUIRE(std::vector<std::string>(first_row) == std::vector<std::string>({
        "123", "345", "678", ""
    }));
}

// Reported in: https://github.com/vincentlaucsb/csv-parser/issues/67
TEST_CASE("Comments in Header Regression", "[comments_in_header_regression]") {
    std::stringstream csv_string(R"(# some extra metadata
# some extra metadata
timestamp,distance,angle,amplitude
22857782,30000,-3141.59,0
22857786,30000,-3141.09,0
)");

    auto format = csv::CSVFormat();
    format.header_row(2);

    csv::CSVReader reader(csv_string, format);

    std::vector<std::string> expected = {
        "timestamp", "distance", "angle", "amplitude"
    };

    // Original issue: Leading comments appeared in column names
    REQUIRE(expected == reader.get_col_names());
}

// Reported in: https://github.com/vincentlaucsb/csv-parser/issues/92
TEST_CASE("Long Row Test", "[long_row_regression]") {
    std::stringstream csv_string;
    constexpr int n_cols = 100000;

    // Make header row
    for (int i = 0; i < n_cols; i++) {
        csv_string << i;
        if (i + 1 == n_cols) {
            csv_string << std::endl;
        }
        else {
            csv_string << ',';
        }
    }

    // Make data row
    for (int i = 0; i < n_cols; i++) {
        csv_string << (double)i * 0.000001;
        if (i + 1 == n_cols) {
            csv_string << std::endl;
        }
        else {
            csv_string << ',';
        }
    }

    auto rows = parse(csv_string.str());
    REQUIRE(rows.get_col_names().size() == n_cols);

    CSVRow row;
    rows.read_row(row);

    int i = 0;

    // Make sure all CSV fields are correct
    for (auto& field : row) {
        std::stringstream temp;
        temp << (double)i * 0.000001;
        REQUIRE(field.get<>() == temp.str());
        i++;
    }
}

// Reported in https://github.com/vincentlaucsb/csv-parser/issues/105
TEST_CASE("Single Column CSV", "[read_single_col_direct]") {
    auto rows = "A\r\n" // Header row
        "123\r\n"
        "1\r\n"
        "4"_csv;

    // Expected results
    size_t i = 0;
    for (auto& row : rows) {
        switch (i) {
        case 0:
            REQUIRE(vector<string>(row) == vector<string>({ "123" }));
            break;
        case 1:
            REQUIRE(vector<string>(row) == vector<string>({ "1" }));
            break;
        case 2:
            REQUIRE(vector<string>(row) == vector<string>({ "4" }));
            break;
        }

        i++;
    }
}

// Regression test for issue #149: trailing newline at EOF must not produce a spurious empty row
TEST_CASE("Trailing newline at EOF (stringstream)", "[trailing_newline_stringstream]") {
    auto check = [](const std::string& csv_text) {
        std::stringstream ss(csv_text);
        CSVFormat format;
        format.no_header();
        CSVReader reader(ss, format);
        size_t row_count = 0;
        for (auto& row : reader) {
            REQUIRE(row.size() > 0);
            row_count++;
        }
        REQUIRE(row_count == 2);
    };

    check("A,B,C\r\n1,2,3\r\n");   // CRLF
    check("A,B,C\n1,2,3\n");       // LF
    check("A,B,C\n1,2,3");         // no trailing newline (control)
}
