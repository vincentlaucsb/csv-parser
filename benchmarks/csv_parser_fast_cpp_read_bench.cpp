#include "bench_common.hpp"

#ifdef CSV_BENCH_HAS_GLAZE
#include "glaze_csv_bench_common.hpp"
#endif

#include <csv.hpp>
#include <csv.h>

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

    const std::string& bench_file() {
        return csv_bench::input_path();
    }

    csv::CSVFormat csv_parser_format(size_t requested_threads, bool background_thread = true) {
        csv::CSVFormat format;
        format.delimiter(',').header_row(0)
            .speculative_parallel_min_bytes(1)
            .speculative_parallel_threads(requested_threads);
        if (!background_thread) {
            format.threading(false);
        }
        return format;
    }

    void set_csv_parser_counters(
        benchmark::State& state,
        size_t requested_threads,
        size_t parser_threads,
        bool background_thread,
        const csv::internals::SpeculativeParseDiagnostics& diagnostics
    ) {
        state.counters["requested_threads"] = static_cast<double>(requested_threads);
        state.counters["parser_threads"] = static_cast<double>(parser_threads);
        state.counters["background_threads"] = background_thread ? 1.0 : 0.0;
        state.counters["spec_chunks"] = static_cast<double>(diagnostics.chunks);
        state.counters["repairs"] = static_cast<double>(diagnostics.validation_repairs);
    }

    void set_fast_cpp_counters(benchmark::State& state) {
        state.counters["parser_threads"] = 1.0;
        state.counters["background_threads"] = 1.0;
    }

    void set_glaze_counters(benchmark::State& state) {
        state.counters["parser_threads"] = 1.0;
        state.counters["background_threads"] = 0.0;
    }

    std::uint64_t read_csv_parser_rows(
        const std::string& path,
        const csv::CSVFormat& format,
        std::size_t& rows,
        size_t& parser_threads,
        csv::internals::SpeculativeParseDiagnostics& diagnostics
    ) {
        std::uint64_t checksum = 0;
        std::size_t current_rows = 0;

        csv::CSVReader reader(path, format);
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
        return checksum;
    }

    void run_csv_parser_read_benchmark(
        benchmark::State& state,
        size_t requested_threads,
        bool background_thread
    ) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        const auto format = csv_parser_format(requested_threads, background_thread);
        std::size_t rows = 0;
        size_t parser_threads = 1;
        csv::internals::SpeculativeParseDiagnostics diagnostics;

        for (auto _ : state) {
            const auto checksum = read_csv_parser_rows(path, format, rows, parser_threads, diagnostics);
            benchmark::DoNotOptimize(checksum);
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
        set_csv_parser_counters(state, requested_threads, parser_threads, background_thread, diagnostics);
    }

    void BM_csv_parser_no_background_thread_read_8col(benchmark::State& state) {
        run_csv_parser_read_benchmark(state, 1, false);
    }

    void BM_csv_parser_spsc_read_8col(benchmark::State& state) {
        run_csv_parser_read_benchmark(state, 1, true);
    }

    void BM_csv_parser_speculative_read_8col(benchmark::State& state) {
        const size_t requested_threads = static_cast<size_t>(state.range(0));
        run_csv_parser_read_benchmark(state, requested_threads, true);
    }

    void BM_fast_cpp_csv_parser_spsc_read_8col(benchmark::State& state) {
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

#ifdef CSV_BENCH_HAS_GLAZE
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
        set_glaze_counters(state);
    }
#endif

    BENCHMARK(BM_csv_parser_no_background_thread_read_8col)
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);

    BENCHMARK(BM_csv_parser_spsc_read_8col)
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);

    BENCHMARK(BM_csv_parser_speculative_read_8col)
        ->ArgName("requested_threads")
        ->Arg(2)
        ->Arg(4)
        ->Arg(8)
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);

    BENCHMARK(BM_fast_cpp_csv_parser_spsc_read_8col)
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);

#ifdef CSV_BENCH_HAS_GLAZE
    BENCHMARK(BM_glaze_csv_read_8col)
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);
#endif
}

CSV_BENCHMARK_MAIN()
