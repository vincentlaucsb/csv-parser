#include "bench_common.hpp"
#include "multi_pass_etl.hpp"

#include <csv.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace {
    const std::string& bench_file() {
        return csv_bench::input_path();
    }

    csv::CSVFormat bench_format() {
        csv::CSVFormat format;
        format.delimiter(',').header_row(0);
        return format;
    }

    std::vector<csv::CSVRow> materialize_csv_rows(const std::string& path) {
        csv::CSVReader reader(path, bench_format());
        std::vector<csv::CSVRow> rows;

        for (auto& row : reader) {
            rows.push_back(row);
        }

        return rows;
    }

    void BM_csv_parser_materialize_csvrow_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            auto materialized = materialize_csv_rows(path);
            rows = materialized.size();
            benchmark::DoNotOptimize(materialized.data());
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_csv_parser_multi_pass_csvrow_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        const auto rows = materialize_csv_rows(path);

        for (auto _ : state) {
            const auto checksum = csv_bench::run_multi_pass_etl(rows, [](const csv::CSVRow& row, size_t index) {
                return row[index].template get<csv::string_view>();
            });

            benchmark::DoNotOptimize(checksum);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows.size());
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_csv_parser_materialize_and_multi_pass_csvrow_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            auto materialized = materialize_csv_rows(path);
            rows = materialized.size();

            const auto checksum = csv_bench::run_multi_pass_etl(materialized, [](const csv::CSVRow& row, size_t index) {
                return row[index].template get<csv::string_view>();
            });

            benchmark::DoNotOptimize(checksum);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    BENCHMARK(BM_csv_parser_materialize_csvrow_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_csv_parser_multi_pass_csvrow_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_csv_parser_materialize_and_multi_pass_csvrow_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
}

CSV_BENCHMARK_MAIN()
