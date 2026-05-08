#include "bench_common.hpp"

#include <csv.hpp>
#include <csv.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {
    using quoted_csv_reader = io::CSVReader<
        8,
        io::trim_chars<' '>,
        io::double_quote_escape<',', '"'>
    >;

    const std::string& bench_file() {
        return csv_bench::input_path();
    }

    csv::CSVFormat bench_format(size_t requested_threads) {
        csv::CSVFormat format;
        format.delimiter(',').header_row(0)
            .speculative_parallel_min_bytes(1)
            .speculative_parallel_threads(requested_threads);
        return format;
    }

    void set_csv_parser_counters(
        benchmark::State& state,
        size_t requested_threads,
        size_t parser_threads,
        const csv::internals::SpeculativeParseDiagnostics& diagnostics
    ) {
        state.counters["requested_threads"] = static_cast<double>(requested_threads);
        state.counters["parser_threads"] = static_cast<double>(parser_threads);
        state.counters["spec_chunks"] = static_cast<double>(diagnostics.chunks);
        state.counters["repairs"] = static_cast<double>(diagnostics.validation_repairs);
    }

    void set_fast_cpp_counters(benchmark::State& state) {
        state.counters["parser_threads"] = 1.0;
    }

    void BM_csv_parser_mmap_positional_read(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        const size_t requested_threads = static_cast<size_t>(state.range(0));
        std::size_t rows = 0;
        size_t parser_threads = 1;
        csv::internals::SpeculativeParseDiagnostics diagnostics;

        for (auto _ : state) {
            std::uint64_t checksum = 0;
            std::size_t current_rows = 0;

            csv::CSVReader reader(path, bench_format(requested_threads));
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
            parser_threads = reader.parse_worker_count();
            diagnostics = reader.speculative_diagnostics();
            benchmark::DoNotOptimize(checksum);
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
        set_csv_parser_counters(state, requested_threads, parser_threads, diagnostics);
    }

    void BM_fast_cpp_csv_parser_read_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            std::uint64_t checksum = 0;
            std::size_t current_rows = 0;

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

            std::string id;
            std::string city;
            std::string state_name;
            std::string category;
            std::string amount;
            std::string quantity;
            std::string flag;
            std::string note;

            while (reader.read_row(id, city, state_name, category, amount, quantity, flag, note)) {
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
        set_fast_cpp_counters(state);
    }

    BENCHMARK(BM_csv_parser_mmap_positional_read)
        ->ArgName("requested_threads")
        ->Arg(1)
        ->Arg(2)
        ->Arg(4)
        ->Arg(8)
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);

    BENCHMARK(BM_fast_cpp_csv_parser_read_8col)
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);
}

CSV_BENCHMARK_MAIN()
