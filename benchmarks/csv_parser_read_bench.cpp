#include "bench_common.hpp"

#include <csv.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {
    const std::string& bench_file() {
        return csv_bench::input_path();
    }

    csv::CSVFormat bench_format() {
        csv::CSVFormat format;
        format.delimiter(',').header_row(0);
        return format;
    }

    void BM_csv_parser_mmap_count_rows(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            std::size_t current_rows = 0;

            csv::CSVReader reader(path, bench_format());
            for (auto& row : reader) {
                benchmark::DoNotOptimize(row.size());
                ++current_rows;
            }

            rows = current_rows;
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_csv_parser_mmap_read(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            std::uint64_t checksum = 0;
            std::size_t current_rows = 0;

            csv::CSVReader reader(path, bench_format());
            for (auto& row : reader) {
                auto id = row["id"].get<csv::string_view>();
                auto city = row["city"].get<csv::string_view>();
                auto note = row["note"].get<csv::string_view>();
                checksum += id.size() + city.size() + note.size();
                if (!id.empty()) checksum += static_cast<unsigned char>(id[0]);
                if (!note.empty()) checksum += static_cast<unsigned char>(note[0]);
                ++current_rows;
            }

            rows = current_rows;
            benchmark::DoNotOptimize(checksum);
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_csv_parser_mmap_positional_read(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            std::uint64_t checksum = 0;
            std::size_t current_rows = 0;

            csv::CSVReader reader(path, bench_format());
            for (auto& row : reader) {
                auto id = row[0].get<csv::string_view>();
                auto city = row[1].get<csv::string_view>();
                auto note = row[7].get<csv::string_view>();
                checksum += id.size() + city.size() + note.size();
                if (!id.empty()) checksum += static_cast<unsigned char>(id[0]);
                if (!note.empty()) checksum += static_cast<unsigned char>(note[0]);
                ++current_rows;
            }

            rows = current_rows;
            benchmark::DoNotOptimize(checksum);
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    BENCHMARK(BM_csv_parser_mmap_count_rows)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_csv_parser_mmap_positional_read)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_csv_parser_mmap_read)->UseRealTime()->Unit(benchmark::kMillisecond);
}

CSV_BENCHMARK_MAIN()
