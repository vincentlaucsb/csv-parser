#pragma once

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace csv_bench {
    inline std::string& input_path_storage() {
        static std::string path;
        return path;
    }

    inline const std::string& input_path() {
        const auto& path = input_path_storage();
        if (path.empty()) {
            throw std::runtime_error("Benchmark input path was not initialized.");
        }

        return path;
    }

    inline void set_items_processed(benchmark::State& state, std::size_t rows) {
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(rows));
    }

    inline void set_bytes_processed(benchmark::State& state, std::uintmax_t bytes) {
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(bytes));
    }

    inline int benchmark_main(int argc, char** argv) {
        benchmark::Initialize(&argc, argv);

        if (argc != 2) {
            throw std::runtime_error("Usage: benchmark_binary [benchmark flags] <csv-file>");
        }

        input_path_storage() = argv[1];

        benchmark::RunSpecifiedBenchmarks();
        benchmark::Shutdown();
        return 0;
    }
}

#define CSV_BENCHMARK_MAIN()                       \
    int main(int argc, char** argv) {              \
        try {                                      \
            return ::csv_bench::benchmark_main(argc, argv); \
        } catch (const std::exception& e) {        \
            std::cerr << e.what() << '\n';         \
            return 1;                              \
        }                                          \
    }
