#include "bench_common.hpp"
#include "glaze_csv_bench_common.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {
    const std::string& bench_file() {
        return csv_bench::input_path();
    }

    void BM_glaze_csv_count_rows_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            const auto buffer = csv_bench::read_file_to_string(path);
            std::vector<csv_bench::glaze_row> materialized;
            const auto error = glz::read<csv_bench::glaze_csv_options>(materialized, buffer);
            if (error) {
                throw std::runtime_error("Glaze CSV read failed");
            }
            rows = materialized.size();
            benchmark::DoNotOptimize(rows);
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_glaze_csv_read_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            std::uint64_t checksum = 0;
            const auto buffer = csv_bench::read_file_to_string(path);
            std::vector<csv_bench::glaze_row> materialized;
            const auto error = glz::read<csv_bench::glaze_csv_options>(materialized, buffer);
            if (error) {
                throw std::runtime_error("Glaze CSV read failed");
            }

            for (const auto& row : materialized) {
                checksum += row.id.size() + row.city.size() + row.note.size();
                if (!row.id.empty()) checksum += static_cast<unsigned char>(row.id[0]);
                if (!row.note.empty()) checksum += static_cast<unsigned char>(row.note[0]);
            }

            rows = materialized.size();
            benchmark::DoNotOptimize(checksum);
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    BENCHMARK(BM_glaze_csv_count_rows_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_glaze_csv_read_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
}

CSV_BENCHMARK_MAIN()
