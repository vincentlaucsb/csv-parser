#include "bench_common.hpp"
#include "multi_pass_etl.hpp"

#include <csv.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {
    using quoted_csv_reader = io::CSVReader<
        8,
        io::trim_chars<' '>,
        io::double_quote_escape<',', '"'>
    >;

    using materialized_row = std::array<std::string, 8>;

    const std::string& bench_file() {
        return csv_bench::input_path();
    }

    std::vector<materialized_row> materialize_fast_cpp_rows(const std::string& path) {
        quoted_csv_reader reader(path.c_str());
        reader.read_header(
            io::ignore_extra_column,
            "id",
            "city",
            "state",
            "category",
            "amount",
            "quantity",
            "flag",
            "note"
        );

        std::vector<materialized_row> rows;
        materialized_row row;

        while (reader.read_row(row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7])) {
            rows.push_back(row);
        }

        return rows;
    }

    void BM_fast_cpp_csv_parser_materialize_array_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            auto materialized = materialize_fast_cpp_rows(path);
            rows = materialized.size();
            benchmark::DoNotOptimize(materialized.data());
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_fast_cpp_csv_parser_multi_pass_array_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        const auto rows = materialize_fast_cpp_rows(path);

        for (auto _ : state) {
            const auto checksum = csv_bench::run_multi_pass_etl(rows, [](const materialized_row& row, size_t index) {
                return std::string_view(row[index]);
            });

            benchmark::DoNotOptimize(checksum);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows.size());
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_fast_cpp_csv_parser_materialize_and_multi_pass_array_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            auto materialized = materialize_fast_cpp_rows(path);
            rows = materialized.size();

            const auto checksum = csv_bench::run_multi_pass_etl(materialized, [](const materialized_row& row, size_t index) {
                return std::string_view(row[index]);
            });

            benchmark::DoNotOptimize(checksum);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    BENCHMARK(BM_fast_cpp_csv_parser_materialize_array_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_fast_cpp_csv_parser_multi_pass_array_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_fast_cpp_csv_parser_materialize_and_multi_pass_array_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
}

CSV_BENCHMARK_MAIN()
