#include <cmath>
#include <cstring>

#include <catch2/catch_all.hpp>
#include "csv.hpp"

using namespace csv;

namespace {
    std::istringstream make_people_stream() {
        return std::istringstream(
            "id,name,value\n"
            "1,Alice,10\n"
            "2,Bob,20\n"
            "1,Carol,30\n"
        );
    }

    const std::string PERSONS_CSV = "./tests/data/mimesis_data/persons.csv";

    struct ColumnStats {
        long double mean = 0;
        long double variance_accumulator = 0;
        long double min = NAN;
        long double max = NAN;
        long double count = 0;
        std::unordered_map<std::string, size_t> value_counts;
        std::unordered_map<DataType, size_t> type_counts;
    };

    struct ChunkStatsResult {
        std::vector<std::string> col_names;
        std::vector<ColumnStats> columns;

        std::vector<long double> means() const {
            std::vector<long double> out;
            out.reserve(columns.size());
            for (const auto& col : columns) {
                out.push_back(col.mean);
            }
            return out;
        }

        std::vector<long double> variances() const {
            std::vector<long double> out;
            out.reserve(columns.size());
            for (const auto& col : columns) {
                out.push_back(col.count > 1 ? col.variance_accumulator / (col.count - 1) : 0);
            }
            return out;
        }

        std::vector<long double> mins() const {
            std::vector<long double> out;
            out.reserve(columns.size());
            for (const auto& col : columns) {
                out.push_back(col.min);
            }
            return out;
        }

        std::vector<long double> maxes() const {
            std::vector<long double> out;
            out.reserve(columns.size());
            for (const auto& col : columns) {
                out.push_back(col.max);
            }
            return out;
        }
    };

    void observe_field(CSVField field, ColumnStats& stats) {
        stats.type_counts[field.type()]++;
        stats.value_counts[field.get<std::string>()]++;

        if (!field.is_num()) {
            return;
        }

        const long double x = field.get<long double>();
        stats.count++;

        if (stats.count == 1) {
            stats.mean = x;
            stats.min = x;
            stats.max = x;
            return;
        }

        const long double delta = x - stats.mean;
        stats.mean += delta / stats.count;
        const long double delta2 = x - stats.mean;
        stats.variance_accumulator += delta * delta2;

        if (x < stats.min) {
            stats.min = x;
        } else if (x > stats.max) {
            stats.max = x;
        }
    }

    ChunkStatsResult compute_chunk_stats(CSVReader& reader, size_t chunk_size = 5000) {
        ChunkStatsResult result;
        result.col_names = reader.get_col_names();
        result.columns.resize(result.col_names.size());
        DataFrameExecutor executor(2);

        chunk_parallel_apply(reader, executor, result.columns,
            [](DataFrame<>::column_type column, ColumnStats& stats) {
                for (size_t row_index = 0; row_index < column.size(); ++row_index) {
                    observe_field(CSVField(column[row_index].get_sv()), stats);
                }
            },
            chunk_size
        );

        return result;
    }
}

TEST_CASE("DataFrame ETL: chunk_parallel_apply uses the common path", "[data_frame][etl]") {
    auto input = make_people_stream();
    CSVReader reader(input);

    struct ColumnSummary {
        std::string first_value;
        size_t non_empty = 0;
    };

    std::vector<ColumnSummary> summaries(reader.get_col_names().size());

    chunk_parallel_apply(reader, summaries,
        [](DataFrame<>::column_type column, ColumnSummary& summary) {
            for (size_t row_index = 0; row_index < column.size(); ++row_index) {
                const std::string value = column[row_index].get<std::string>();
                if (summary.first_value.empty()) {
                    summary.first_value = value;
                }

                if (!value.empty()) {
                    summary.non_empty++;
                }
            }
        },
        2
    );

    REQUIRE(summaries.size() == 3);
    REQUIRE(summaries[0].first_value == "1");
    REQUIRE(summaries[1].first_value == "Alice");
    REQUIRE(summaries[2].first_value == "10");

    REQUIRE(summaries[0].non_empty == 3);
    REQUIRE(summaries[1].non_empty == 3);
    REQUIRE(summaries[2].non_empty == 3);
}

TEST_CASE("DataFrame ETL: column_parallel_apply sees edited values", "[data_frame][etl]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

    frame["1"]["name"] = "Alicia";
    frame["2"]["value"] = "22";

    struct ColumnSummary {
        std::string first_value;
        size_t non_empty = 0;
    };

    std::vector<ColumnSummary> summaries(frame.n_cols());
    DataFrameExecutor executor(2);

    frame.column_parallel_apply(executor, summaries,
        [](DataFrame<>::column_type column, ColumnSummary& summary) {
            for (size_t row_index = 0; row_index < column.size(); ++row_index) {
                const std::string value = column[row_index].get<std::string>();
                if (summary.first_value.empty()) {
                    summary.first_value = value;
                }

                if (!value.empty()) {
                    summary.non_empty++;
                }
            }
        }
    );

    REQUIRE(summaries.size() == 3);
    REQUIRE(summaries[0].first_value == "1");
    REQUIRE(summaries[1].first_value == "Alicia");
    REQUIRE(summaries[2].first_value == "10");

    REQUIRE(summaries[0].non_empty == 2);
    REQUIRE(summaries[1].non_empty == 2);
    REQUIRE(summaries[2].non_empty == 2);
}

TEST_CASE("DataFrame ETL: column_parallel_apply validates state count", "[data_frame][etl]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader);

    std::vector<int> bad_states(frame.n_cols() - 1, 0);
    DataFrameExecutor executor(1);

    REQUIRE_THROWS_AS(
        frame.column_parallel_apply(executor, bad_states,
            [](DataFrame<>::column_type, int&) {}
        ),
        std::invalid_argument
    );
}

TEST_CASE("DataFrame ETL: chunk_parallel_apply validates state count", "[data_frame][etl]") {
    auto input = make_people_stream();
    CSVReader reader(input);

    std::vector<int> bad_states(reader.get_col_names().size() - 1, 0);

    REQUIRE_THROWS_AS(
        chunk_parallel_apply(reader, bad_states,
            [](DataFrame<>::column_type, int&) {},
            2
        ),
        std::invalid_argument
    );
}

TEST_CASE("DataFrame ETL: column_parallel_apply propagates worker exceptions", "[data_frame][etl]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader);
    DataFrameExecutor executor(2);
    std::vector<int> states(frame.n_cols(), 0);

    REQUIRE_THROWS_AS(
        frame.column_parallel_apply(executor, states,
            [](DataFrame<>::column_type column, int&) {
                if (column.name() == "name") {
                    throw std::runtime_error("column failure");
                }
            }
        ),
        std::runtime_error
    );

    std::vector<size_t> row_counts(frame.n_cols(), 0);
    REQUIRE_NOTHROW(
        frame.column_parallel_apply(executor, row_counts,
            [](DataFrame<>::column_type column, size_t& count) {
                count = column.size();
            }
        )
    );

    REQUIRE(row_counts.size() == frame.n_cols());
    REQUIRE(row_counts[0] == frame.n_rows());
    REQUIRE(row_counts[1] == frame.n_rows());
    REQUIRE(row_counts[2] == frame.n_rows());
}

#ifndef __EMSCRIPTEN__
TEST_CASE("DataFrame ETL: csv_data_types uses chunked executor path", "[data_frame][etl][csv_data_types]") {
    auto dtypes = csv_data_types(PERSONS_CSV);

    REQUIRE(dtypes["Full Name"] == DataType::CSV_STRING);
    REQUIRE(dtypes["Age"] == DataType::CSV_INT8);
    REQUIRE(dtypes["Occupation"] == DataType::CSV_STRING);
    REQUIRE(dtypes["Email"] == DataType::CSV_STRING);
    REQUIRE(dtypes["Telephone"] == DataType::CSV_STRING);
    REQUIRE(dtypes["Nationality"] == DataType::CSV_STRING);
}
#endif

TEST_CASE("DataFrame ETL: csv_data_types forwards CSVReader constructor arguments", "[data_frame][etl][csv_data_types]") {
    std::istringstream input(
        "name,age,score\n"
        "Alice,30,1.5\n"
        "Bob,41,2.0\n"
    );

    CSVFormat format;
    format.delimiter(',').header_row(0);

    auto dtypes = csv_data_types(input, format);

    REQUIRE(dtypes["name"] == DataType::CSV_STRING);
    REQUIRE(dtypes["age"] == DataType::CSV_INT8);
    REQUIRE(dtypes["score"] == DataType::CSV_DOUBLE);
}

#ifndef __EMSCRIPTEN__
TEST_CASE("ETL stats helper: missing file surfaces reader error", "[data_frame][etl][stats]") {
    bool error_caught = false;

    try {
        auto dtypes = csv_data_types("./tests/data/fake_data/empty.csv");
        (void)dtypes;
    }
    catch (std::runtime_error& err) {
        error_caught = true;
        REQUIRE(strcmp(err.what(), "Cannot open file ./tests/data/fake_data/empty.csv") == 0);
    }

    REQUIRE(error_caught);
}
#endif

TEST_CASE("ETL stats helper: direct input", "[data_frame][etl][stats]") {
    std::string int_str;
    std::stringstream int_list;
    for (int i = 1; i < 101; i++) {
        int_str = std::to_string(i);
        int_list << int_str << "," << int_str << "," << int_str << "\r\n";
    }

    CSVFormat format;
    format.column_names({ "A", "B", "C" });

    CSVReader reader(int_list, format);
    auto stats = compute_chunk_stats(reader);

    std::vector<long double> means = { 50.5, 50.5, 50.5 };
    std::vector<long double> mins = { 1, 1, 1 };
    std::vector<long double> maxes = { 100, 100, 100 };

    REQUIRE(stats.mins() == mins);
    REQUIRE(stats.maxes() == maxes);
    REQUIRE(stats.means() == means);
    REQUIRE(ceill(stats.variances()[0]) == 842);

    for (int i = 1; i < 101; i++) {
        REQUIRE(stats.columns[0].value_counts[std::to_string(i)] == 1);
    }

    REQUIRE(stats.columns[0].type_counts[DataType::CSV_INT8] == 100);
}

#ifndef __EMSCRIPTEN__
TEST_CASE("ETL stats helper: integer rows", "[data_frame][etl][stats]") {
    auto file = GENERATE(as<std::string> {},
        "./tests/data/fake_data/ints.csv",
        "./tests/data/fake_data/ints_newline_sep.csv"
    );

    CSVReader reader(file);
    auto stats = compute_chunk_stats(reader);

    std::vector<long double> means = {
        50.5, 50.5, 50.5, 50.5, 50.5,
        50.5, 50.5, 50.5, 50.5, 50.5
    };

    REQUIRE(stats.means() == means);
    REQUIRE(stats.mins()[0] == 1);
    REQUIRE(stats.maxes()[0] == 100);
    REQUIRE(ceill(stats.variances()[0]) == 842);
}

TEST_CASE("ETL stats helper: persons.csv", "[data_frame][etl][stats]") {
    CSVReader reader(PERSONS_CSV);
    auto stats = compute_chunk_stats(reader);

    REQUIRE(stats.maxes()[0] == 49999);
    REQUIRE(ceill(stats.means()[2]) == 42);
}
#endif
