#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/file_guard.hpp"
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>

using namespace csv;

// Assert that an error is thrown if whitespace, delimiter, and quote 
TEST_CASE("CSVFormat - Overlapping Characters", "[csv_format_overlap]") {
    CSVFormat format;
    bool err_caught = false;

    SECTION("Tab") {
        try {
            format.delimiter('\t').quote('"').trim({ '\t' });
        }
        catch (std::runtime_error& err) {
            err_caught = true;
            REQUIRE(err.what() == internals::make_char_overlap_error({ '\t' }));
        }

        REQUIRE(err_caught);
    }

    SECTION("Tab with multiple other characters") {
        try {
            format.delimiter({ ',', '\t' }).quote('"').trim({ ' ', '\t' });
        }
        catch (std::runtime_error& err) {
            err_caught = true;
            REQUIRE(err.what() == internals::make_char_overlap_error({ '\t' }));
        }

        REQUIRE(err_caught);
    }

    SECTION("Repeated quote") {
        try {
            format.delimiter({ ',', '"' }).quote('"').trim({ ' ', '\t' });
        }
        catch (std::runtime_error& err) {
            err_caught = true;
            REQUIRE(err.what() == internals::make_char_overlap_error({ '"' }));
        }

        REQUIRE(err_caught);
    }

    SECTION("Multiple offenders") {
        try {
            format.delimiter({ ',', '\t', ' ' }).quote('"').trim({ ' ', '\t' });
        }
        catch (std::runtime_error& err) {
            err_caught = true;
            REQUIRE(err.what() == internals::make_char_overlap_error({ '\t', ' ' }));
        }

        REQUIRE(err_caught);
    }
}
/* Ensure no_header() works correctly with delimiter guessing
 * 
 * Reported in: https://github.com/vincentlaucsb/csv-parser/issues/285
 * 
 * When using .no_header() with multiple delimiters (which triggers guessing),
 * the guessing logic was overwriting the header=-1 setting, causing the first
 * data row to be skipped.
 */
TEST_CASE("CSVFormat - no_header() with Delimiter Guessing (Issue #285)", "[csv_format_no_header]") {
    // Verify delimiter guessing preserves no_header() setting
    std::string csv_string = "row\t1\n"
                             "row\t2\n"
                             "row\t3\n";
    
    SECTION("Multiple delimiters + no_header()") {
        CSVFormat format;
        format.delimiter({'\t', ';'})  // Multiple delimiters triggers guessing
              .no_header();             // Should preserve all rows
        
        std::stringstream source(csv_string);
        CSVReader reader(source, format);
        
        std::vector<CSVRow> rows;
        for (auto& row : reader) {
            rows.push_back(row);
        }
        
        // Assert CORRECT behavior: All 3 rows should be present
        REQUIRE(rows.size() == 3);
        REQUIRE(rows[0][0].get<>() == "row");
        REQUIRE(rows[0][1].get<>() == "1");
        REQUIRE(rows[1][0].get<>() == "row");
        REQUIRE(rows[1][1].get<>() == "2");
        REQUIRE(rows[2][0].get<>() == "row");
        REQUIRE(rows[2][1].get<>() == "3");
    }
    
    SECTION("Single delimiter + no_header() - should work") {
        // Verify single delimiter doesn't trigger the bug
        CSVFormat format;
        format.delimiter('\t')  // Single delimiter, no guessing
              .no_header();
        
        std::stringstream source(csv_string);
        CSVReader reader(source, format);
        
        std::vector<CSVRow> rows;
        for (auto& row : reader) {
            rows.push_back(row);
        }
        
        // Single delimiter path should already work correctly
        REQUIRE(rows.size() == 3);
        REQUIRE(rows[0][0].get<>() == "row");
        REQUIRE(rows[0][1].get<>() == "1");
    }
}

TEST_CASE("CSVFormat - speculative parallel parsing options", "[csv_format]") {
    CSVFormat format;

#if CSV_ENABLE_THREADS
    REQUIRE(format.is_threading_enabled());
#else
    REQUIRE_FALSE(format.is_threading_enabled());
#endif
    REQUIRE(format.get_speculative_parallel_threads() == 0);
    REQUIRE(format.get_speculative_parallel_min_bytes() == internals::CSV_SPECULATIVE_PARALLEL_MIN_BYTES);
    REQUIRE_FALSE(format.is_eager_field_classification_enabled());
#if CSV_ENABLE_THREADS
    REQUIRE(format.should_use_speculative_parallel(
        internals::CSV_SPECULATIVE_PARALLEL_MIN_BYTES,
        4
    ));
#else
    REQUIRE_FALSE(format.should_use_speculative_parallel(
        internals::CSV_SPECULATIVE_PARALLEL_MIN_BYTES,
        4
    ));
#endif

    format.speculative_parallel_threads(4)
        .speculative_parallel_min_bytes(1024);

    REQUIRE(format.get_speculative_parallel_threads() == 4);
    REQUIRE(format.get_speculative_parallel_min_bytes() == 1024);
    REQUIRE_FALSE(format.should_use_speculative_parallel(1023, 4));
    REQUIRE_FALSE(format.should_use_speculative_parallel(1024, 1));
#if CSV_ENABLE_THREADS
    REQUIRE(format.should_use_speculative_parallel(1024, 2));
#else
    REQUIRE_FALSE(format.should_use_speculative_parallel(1024, 2));
#endif
}

TEST_CASE("CSVReader eager field classification preserves typed behavior", "[csv_format][csv_reader][eager_classification]") {
    const std::string data = "plain,\"a\"\"b\", true ,,123,-4.5,1970-01-02T00:00:00.123Z\n";

    auto check_row = [](CSVRow& row) {
        REQUIRE(row.size() == 7);
        REQUIRE(row[0].get<std::string>() == "plain");
        REQUIRE(row[1].get<std::string>() == "a\"b");
        REQUIRE(row[2].get<bool>());
        REQUIRE(row[3].is_null());
        REQUIRE(row[4].get<int>() == 123);
        REQUIRE(row[5].get<double>() == Catch::Approx(-4.5));
        REQUIRE(row[6].is_timestamp());
    };

    SECTION("stream path") {
        std::stringstream input(data);
        CSVFormat format;
        format.no_header()
            .trim({ ' ' })
            .eager_field_classification();
        CSVReader reader(input, format);

        CSVRow row;
        REQUIRE(reader.read_row(row));
        check_row(row);
        REQUIRE_FALSE(reader.read_row(row));
    }

#ifndef __EMSCRIPTEN__
    SECTION("mmap path") {
        FileGuard cleanup("./tests/data/tmp_eager_classification.csv");
        {
            std::ofstream out(cleanup.filename, std::ios::binary);
            out << data;
        }

        CSVFormat format;
        format.no_header()
            .trim({ ' ' })
            .eager_field_classification();
        CSVReader reader(cleanup.filename, format);

        CSVRow row;
        REQUIRE(reader.read_row(row));
        check_row(row);
        REQUIRE_FALSE(reader.read_row(row));
    }
#endif
}

TEST_CASE("CSVFormat - runtime threading switch disables speculative workers", "[csv_format]") {
    CSVFormat format;
    format.threading(false)
        .speculative_parallel_threads(4)
        .speculative_parallel_min_bytes(1);

    REQUIRE_FALSE(format.is_threading_enabled());
    REQUIRE_FALSE(format.should_use_speculative_parallel(1, 4));

    format.threading();
#if CSV_ENABLE_THREADS
    REQUIRE(format.is_threading_enabled());
    REQUIRE(format.should_use_speculative_parallel(1, 4));
#else
    REQUIRE_FALSE(format.is_threading_enabled());
    REQUIRE_FALSE(format.should_use_speculative_parallel(1, 4));
#endif
}

TEST_CASE("CSVFormat - chunk_size rejects values larger than CSV_CHUNK_SIZE_MAX", "[csv_format]") {
    CSVFormat format;

    if ((std::numeric_limits<size_t>::max)() <= internals::CSV_CHUNK_SIZE_MAX) {
        SUCCEED("size_t cannot represent a chunk size larger than CSV_CHUNK_SIZE_MAX on this platform");
        return;
    }

    const size_t too_large = internals::CSV_CHUNK_SIZE_MAX + 1;
    REQUIRE_THROWS_WITH(
        format.chunk_size(too_large),
        internals::make_chunk_size_ceiling_error(internals::CSV_CHUNK_SIZE_MAX, too_large)
    );
}

TEST_CASE("CSVReader honors runtime threading opt-out", "[csv_format][csv_reader]") {
    std::stringstream input(
        "a,b,c\n"
        "1,2,3\n"
        "4,5,6\n"
    );
    CSVFormat format;
    format.no_header()
        .delimiter(',')
        .threading(false)
        .speculative_parallel_min_bytes(1)
        .speculative_parallel_threads(2);

    CSVReader reader(input, format);

    REQUIRE(reader.parse_worker_count() == 1);
    REQUIRE(reader.speculative_diagnostics().chunks == 0);

    std::vector<CSVRow> rows;
    CSVRow row;
    while (reader.read_row(row)) {
        rows.push_back(row);
    }

    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0][0] == "a");
    REQUIRE(rows[2][2] == "6");
}

#ifndef __EMSCRIPTEN__
TEST_CASE("CSVReader honors runtime threading opt-out for filename inputs", "[csv_format][csv_reader]") {
    FileGuard cleanup("./tests/data/tmp_threading_opt_out_mmap.csv");
    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        std::string content = "a,b,c\n";
        while (content.size() <= internals::CSV_CHUNK_SIZE_FLOOR + 1024) {
            content += "1,2,3\n";
        }
        out << content;
    }

    CSVFormat format;
    format.no_header()
        .delimiter(',')
        .chunk_size(internals::CSV_CHUNK_SIZE_FLOOR)
        .threading(false)
        .speculative_parallel_min_bytes(1)
        .speculative_parallel_threads(2);

    CSVReader reader(cleanup.filename, format);
    REQUIRE(reader.parse_worker_count() == 1);

    size_t rows = 0;
    for (auto& row : reader) {
        REQUIRE(row.size() == 3);
        rows++;
    }

    REQUIRE(rows > 1);
    REQUIRE(reader.speculative_diagnostics().chunks == 0);
}
#endif
