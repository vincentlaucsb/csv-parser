#include "bench_common.hpp"

#include <csv.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <unordered_map>
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

    std::uint64_t run_csv_parser_multi_pass_etl(const std::vector<csv::CSVRow>& rows) {
        std::uint64_t amount_sum = 0;
        for (const auto& row : rows) {
            amount_sum += row[4].get<std::uint64_t>();
        }

        std::uint64_t quantity_sum = 0;
        std::uint64_t enabled_count = 0;
        for (const auto& row : rows) {
            quantity_sum += row[5].get<std::uint64_t>();
            enabled_count += row[6].get<csv::string_view>() == std::string_view("Y") ? 1u : 0u;
        }

        std::unordered_map<std::string_view, std::uint64_t> category_counts;
        category_counts.reserve(8);
        for (const auto& row : rows) {
            ++category_counts[row[3].get<csv::string_view>()];
        }

        std::uint64_t text_checksum = 0;
        for (const auto& row : rows) {
            const auto city = row[1].get<csv::string_view>();
            const auto note = row[7].get<csv::string_view>();
            text_checksum += static_cast<std::uint64_t>(city.size() * 3 + note.size());
            if (!city.empty()) {
                text_checksum += static_cast<unsigned char>(city.front());
            }
            if (!note.empty()) {
                text_checksum += static_cast<unsigned char>(note.front());
            }
        }

        std::uint64_t category_checksum = 0;
        for (const auto& entry : category_counts) {
            category_checksum += static_cast<std::uint64_t>(entry.first.size()) * entry.second;
        }

        return amount_sum + quantity_sum + enabled_count + text_checksum + category_checksum;
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
            const auto checksum = run_csv_parser_multi_pass_etl(rows);

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

            const auto checksum = run_csv_parser_multi_pass_etl(materialized);

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
