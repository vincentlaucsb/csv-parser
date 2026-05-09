#include "bench_common.hpp"
#include "glaze_csv_bench_common.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
    const std::string& bench_file() {
        return csv_bench::input_path();
    }

    std::uint64_t run_glaze_multi_pass_etl(const std::vector<csv_bench::glaze_typed_row>& rows) {
        std::uint64_t amount_sum = 0;
        for (const auto& row : rows) {
            amount_sum += row.amount;
        }

        std::uint64_t quantity_sum = 0;
        std::uint64_t enabled_count = 0;
        for (const auto& row : rows) {
            quantity_sum += row.quantity;
            enabled_count += row.flag == "Y" ? 1u : 0u;
        }

        std::unordered_map<std::string_view, std::uint64_t> category_counts;
        category_counts.reserve(8);
        for (const auto& row : rows) {
            ++category_counts[row.category];
        }

        std::uint64_t text_checksum = 0;
        for (const auto& row : rows) {
            text_checksum += static_cast<std::uint64_t>(row.city.size() * 3 + row.note.size());
            if (!row.city.empty()) {
                text_checksum += static_cast<unsigned char>(row.city.front());
            }
            if (!row.note.empty()) {
                text_checksum += static_cast<unsigned char>(row.note.front());
            }
        }

        std::uint64_t category_checksum = 0;
        for (const auto& entry : category_counts) {
            category_checksum += static_cast<std::uint64_t>(entry.first.size()) * entry.second;
        }

        return amount_sum + quantity_sum + enabled_count + text_checksum + category_checksum;
    }

    void BM_glaze_csv_materialize_struct_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            const auto buffer = csv_bench::read_file_to_string(path);
            std::vector<csv_bench::glaze_typed_row> materialized;
            const auto error = glz::read<csv_bench::glaze_csv_options>(materialized, buffer);
            if (error) {
                throw std::runtime_error("Glaze CSV read failed");
            }
            rows = materialized.size();
            benchmark::DoNotOptimize(materialized.data());
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_glaze_csv_multi_pass_struct_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        const auto buffer = csv_bench::read_file_to_string(path);
        std::vector<csv_bench::glaze_typed_row> rows;
        const auto error = glz::read<csv_bench::glaze_csv_options>(rows, buffer);
        if (error) {
            throw std::runtime_error("Glaze CSV read failed");
        }

        for (auto _ : state) {
            const auto checksum = run_glaze_multi_pass_etl(rows);

            benchmark::DoNotOptimize(checksum);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows.size());
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_glaze_csv_materialize_and_multi_pass_struct_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            const auto buffer = csv_bench::read_file_to_string(path);
            std::vector<csv_bench::glaze_typed_row> materialized;
            const auto error = glz::read<csv_bench::glaze_csv_options>(materialized, buffer);
            if (error) {
                throw std::runtime_error("Glaze CSV read failed");
            }
            rows = materialized.size();

            const auto checksum = run_glaze_multi_pass_etl(materialized);

            benchmark::DoNotOptimize(checksum);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    BENCHMARK(BM_glaze_csv_materialize_struct_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_glaze_csv_multi_pass_struct_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_glaze_csv_materialize_and_multi_pass_struct_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
}

CSV_BENCHMARK_MAIN()
