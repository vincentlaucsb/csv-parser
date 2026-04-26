#include "bench_common.hpp"

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

    void BM_fast_cpp_csv_parser_count_rows_8col(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
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
                benchmark::DoNotOptimize(id.size());
                ++current_rows;
            }

            rows = current_rows;
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
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
    }

    BENCHMARK(BM_fast_cpp_csv_parser_count_rows_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_fast_cpp_csv_parser_read_8col)->UseRealTime()->Unit(benchmark::kMillisecond);
}

CSV_BENCHMARK_MAIN()
