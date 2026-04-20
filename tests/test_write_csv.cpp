#include <fstream>
#include <sstream>
#include <queue>
#include <list>
#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/file_guard.hpp"

using namespace csv;
using std::queue;
using std::vector;
using std::string;

#ifndef __clang__
TEST_CASE("Numeric Converter Tsts", "[test_convert_number]") {
    SECTION("num_digits") {
        REQUIRE(csv::internals::num_digits(99.0) == 2);
        REQUIRE(csv::internals::num_digits(100.0) == 3);
    }

    SECTION("Large Numbers") {
        // Large numbers: integer larger than uint64 capacity
        REQUIRE(csv::internals::to_string(200000000000000000000.0) == "200000000000000000000.0");
        REQUIRE(csv::internals::to_string(310000000000000000000.0) == "310000000000000000000.0");
    }

    SECTION("Custom Precision") {
        // Test setting precision
        REQUIRE(csv::internals::to_string(1.234) == "1.23400");
        REQUIRE(csv::internals::to_string(20.0045) == "20.00450");

        set_decimal_places(2);
        REQUIRE(csv::internals::to_string(1.234) == "1.23");

        // Reset
        set_decimal_places(5);
    }

    SECTION("Decimal Numbers x where -1 < x < 0") {
        REQUIRE(csv::internals::to_string(-0.25) == "-0.25000");
        REQUIRE(csv::internals::to_string(-0.625) == "-0.62500");
        REQUIRE(csv::internals::to_string(-0.666) == "-0.66600");
    }

    SECTION("Numbers Close to 10^n - Regression") {
        REQUIRE(csv::internals::to_string(10.0) == "10.0");
        REQUIRE(csv::internals::to_string(100.0) == "100.0");
        REQUIRE(csv::internals::to_string(1000.0) == "1000.0");
        REQUIRE(csv::internals::to_string(10000.0) == "10000.0");
        REQUIRE(csv::internals::to_string(100000.0) == "100000.0");
        REQUIRE(csv::internals::to_string(1000000.0) == "1000000.0");
    }
}
#endif

namespace {
    struct StringOutput {
        std::stringstream stream;

        std::string str() const {
            return stream.str();
        }
    };

#ifndef __EMSCRIPTEN__
    struct FileOutput {
        FileGuard guard;
        std::ofstream stream;

        FileOutput() : guard([]() {
            static int counter = 0;
            return "test_write_csv_output_" + std::to_string(++counter) + ".csv";
        }()) {
            stream.open(guard.filename, std::ios::out | std::ios::trunc);
        }

        std::string str() {
            stream.flush();
            std::ifstream in(guard.filename, std::ios::in);
            std::stringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }
    };
#endif
}

#ifndef __EMSCRIPTEN__
TEMPLATE_TEST_CASE("Basic CSV Writing Cases", "[test_csv_write]", StringOutput, FileOutput) {
    TestType output;
    std::stringstream correct;
    auto writer = make_csv_writer(output.stream);

    SECTION("Escaped Comma") {
        writer << std::array<std::string, 1>({ "Furthermore, this should be quoted." });
        correct << "\"Furthermore, this should be quoted.\"";
    }

    SECTION("Quote Escape") {
        writer << std::array<std::string, 1>({ "\"What does it mean to be RFC 4180 compliant?\" she asked." });
        correct << "\"\"\"What does it mean to be RFC 4180 compliant?\"\" she asked.\"";
    }

    SECTION("Newline Escape") {
        writer << std::array<std::string, 1>({ "Line 1\nLine2" });
        correct << "\"Line 1\nLine2\"";
    }

    SECTION("Leading and Trailing Quote Escape") {
        writer << std::array<std::string, 1>({ "\"\"" });
        correct << "\"\"\"\"\"\"";
    }

    SECTION("Quote Minimal") {
        writer << std::array<std::string, 1>({ "This should not be quoted" });
        correct << "This should not be quoted";
    }
    
    SECTION("Single Column") {
        writer << std::array<std::string, 1>({ "Single column value" });
        correct << "Single column value";
    }

    correct << std::endl;
    REQUIRE(output.str() == correct.str());
}
#endif

#ifndef __EMSCRIPTEN__
TEMPLATE_TEST_CASE("CSV Quote All", "[test_csv_quote_all]", StringOutput, FileOutput) {
    TestType output;
    std::stringstream correct;
    auto writer = make_csv_writer(output.stream, false);

    writer << std::array<std::string, 1>({ "This should be quoted" });
    correct << "\"This should be quoted\"" << std::endl;

    REQUIRE(output.str() == correct.str());
}
#endif

//! [CSV Writer Example]
TEMPLATE_TEST_CASE("CSV/TSV Writer - operator <<", "[test_csv_operator<<]",
    std::vector<std::string>, std::deque<std::string>, std::list<std::string>) {
    std::stringstream output, correct_comma, correct_tab;

    // Build correct strings
    correct_comma << "A,B,C" << std::endl << "\"1,1\",2,3" << std::endl;
    correct_tab << "A\tB\tC" << std::endl << "1,1\t2\t3" << std::endl;

    // Test input
    auto test_row_1 = TestType({ "A", "B", "C" }),
        test_row_2 = TestType({ "1,1", "2", "3" });

    SECTION("CSV Writer") {
        auto csv_writer = make_csv_writer(output);
        csv_writer << test_row_1 << test_row_2;

        REQUIRE(output.str() == correct_comma.str());
    }

    SECTION("TSV Writer") {
        auto tsv_writer = make_tsv_writer(output);
        tsv_writer << test_row_1 << test_row_2;

        REQUIRE(output.str() == correct_tab.str());
    }
}
//! [CSV Writer Example]

//! [CSV write_row Variadic Example]
TEST_CASE("CSV Writer - write_row() with variadic fields", "[test_csv_write_row_variadic]") {
    std::stringstream output, correct;
    auto writer = make_csv_writer(output);

    // Write rows with mixed types using write_row()
    writer.write_row("Name", "Age", "Score");
    writer.write_row("Alice", 30, 95.5);
    writer.write_row("Bob", 25, 87.3);
    writer.write_row("Charlie", 35, 92.8);

    correct << "Name,Age,Score" << std::endl
        << "Alice,30,95.5" << std::endl
        << "Bob,25,87.3" << std::endl
        << "Charlie,35,92.8" << std::endl;

    REQUIRE(output.str() == correct.str());
}
//! [CSV write_row Variadic Example]

//! [CSV Writer Tuple Example]
struct Time {
    std::string hour;
    std::string minute;

    operator std::string() const {
        std::string ret = hour;
        ret += ":";
        ret += minute;
        
        return ret;
    }
};

#ifndef __clang__
TEST_CASE("CSV Tuple", "[test_csv_tuple]") {
    #ifdef CSV_HAS_CXX17
    Time time = { "5", "30" };
    #else
    std::string time = "5:30";
    #endif
    std::stringstream output, correct_output;
    auto csv_writer = make_csv_writer(output);

    csv_writer << std::make_tuple("One", 2, "Three", 4.0, time)
        << std::make_tuple("One", (short)2, "Three", 4.0f, time)
        << std::make_tuple(-1, -2.0)
        << std::make_tuple(20.2, -20.3, -20.123)
        << std::make_tuple(0.0, 0.0f, 0);

    correct_output << "One,2,Three,4.0,5:30" << std::endl
        << "One,2,Three,4.0,5:30" << std::endl
        << "-1,-2.0" << std::endl
        << "20.19999,-20.30000,-20.12300" << std::endl
        << "0.0,0.0,0" << std::endl;

    REQUIRE(output.str() == correct_output.str());
}
#endif
//! [CSV Writer Tuple Example]

//! [CSV Reordering Example]
TEST_CASE("CSV Writer - Reorder Columns", "[test_csv_reorder]") {
    auto rows = "A,B,C\r\n"
        "1,2,3\r\n"
        "4,5,6"_csv;

    std::stringstream output, correct;
    auto writer = make_csv_writer(output);

    writer << std::vector<std::string>({ "C", "A" });
    for (auto& row : rows) {
        writer << std::vector<std::string>({
            row[csv::string_view("C")].get<std::string>(),
            row[csv::string_view("A")].get<std::string>()
        });
    }

    correct << "C,A" << std::endl
        << "3,1" << std::endl
        << "6,4" << std::endl;

    REQUIRE(output.str() == correct.str());
}
//! [CSV Reordering Example]

//! [CSV Ranges Reordering Example]
#ifdef CSV_HAS_CXX20
#include <ranges>

TEST_CASE("CSV Writer - Reorder with Ranges", "[test_csv_reorder_ranges]") {
    auto rows = "A,B,C\r\n"
        "1,2,3\r\n"
        "4,5,6"_csv;

    std::stringstream output, correct;
    auto writer = make_csv_writer(output);

    // Write header: C, A
    writer << std::vector<std::string>({ "C", "A" });

    // Reorder columns using ranges::views::transform with string_view
    for (auto& row : rows) {
        std::vector<std::string_view> field_names = { "C", "A" };
        auto reordered = field_names
            | std::views::transform([&row](std::string_view field) {
                return row[field];
            });
        writer << reordered;
    }

    correct << "C,A" << std::endl
        << "3,1" << std::endl
        << "6,4" << std::endl;

    REQUIRE(output.str() == correct.str());
}
#endif
//! [CSV Ranges Reordering Example]

//! [DataFrame Sparse Overlay Write Example]
TEST_CASE("DataFrame - Write with Sparse Overlay", "[test_dataframe_sparse_overlay_write]") {
    auto reader = 
        "id,name,age,occupation,react_experience_years,favorite_hook,quote\n"
        "1,Chad Hooks,28,Senior React Engineer,5,useCallback,\"My useCallback has 12 dependencies and I'm scared to remove any\"\n"
        "2,Tailwind Tim,24,Frontend Architect,3,useEffect,\"I fixed the infinite loop by adding another useEffect\"\n"
        "3,Dan Abramov Disciple,31,Principal React Engineer,7,useMemo,\"If it's not memoized it's not React\"\n"
        "6,Class Component Carl,42,Legacy React Dev,12,none,\"Remember when React was fun? Pepperidge Farm remembers.\""_csv;
    
    csv::DataFrame<std::string> df(reader);
    
    // Make sparse edits to specific cells using the overlay
    df.set("1", "age", "29");  // Chad Hooks has a birthday
    df.set("3", "react_experience_years", "8");  // Dan got one more year
    df.set("6", "quote", "Everything is fine in production");  // Updated quote
    
    // Write the modified DataFrame back
    std::stringstream output;
    auto writer = csv::make_csv_writer(output);
    
    writer << df.columns();
    for (auto& row : df) {
#ifdef CSV_HAS_CXX20
        // More efficient version with C++20 ranges
        writer << row.to_sv_range();
#else
        writer << std::vector<std::string>(row);
#endif
    }
    
    // Verify the sparse edits are in the output
    std::string result = output.str();
    REQUIRE(result.find("1,Chad Hooks,29,") != std::string::npos);  // age updated
    REQUIRE(result.find("3,Dan Abramov Disciple,31,Principal React Engineer,8,") != std::string::npos);  // experience updated
    REQUIRE(result.find("Everything is fine in production") != std::string::npos);  // quote updated
}
//! [DataFrame Sparse Overlay Write Example]
