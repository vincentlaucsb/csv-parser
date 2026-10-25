#pragma once

#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "file_guard.hpp"
#include "generated_file.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <streambuf>
#include <vector>

static_assert(CSV_EXCEPTIONS_ENABLED == CSV_TEST_EXCEPTIONS,
    "These tests must compile in the requested exception mode.");

namespace csv_test { namespace explicit_errors {
    const size_t large_row_count = 500000;

    // Create only the requested guard: independent test executables may run in
    // parallel, so an unused fixture must not remove another suite's live file.
    inline GeneratedFile& large_csv_fixture(bool malformed, bool early_short) {
        if (early_short) {
            static GeneratedFile file("tests/data/fake_data/explicit_errors_early_short.csv");
            return file;
        }
        if (malformed) {
            static GeneratedFile file("tests/data/fake_data/explicit_errors_late_bad.csv");
            return file;
        }
        static GeneratedFile file("tests/data/fake_data/explicit_errors_large.csv");
        return file;
    }

    inline const std::string& large_csv(bool malformed, bool early_short = false) {
        return large_csv_fixture(malformed, early_short).path(
            [malformed, early_short](std::ostream& out) {
                out << "a,b,c,d,e\n";
                for (size_t i = 0; i < large_row_count; ++i) {
                    const size_t fields = early_short && i == 100 ? 4 : 5;
                    for (size_t j = 0; j < fields; ++j) {
                        if (j) out << ',';
                        out << i * 5 + j;
                    }
                    out << '\n';
                }
                if (malformed) out << "1,2,3,4,5,6\n";
            });
    }

    inline csv::CSVFormat large_format(bool threaded) {
        csv::CSVFormat format;
        format.header_row(0).variable_columns(csv::VariableColumnPolicy::THROW).threading(threaded);
        return format;
    }
}}
