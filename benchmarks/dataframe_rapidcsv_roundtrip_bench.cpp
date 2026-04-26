#include "bench_common.hpp"

#include <csv.hpp>
#include <rapidcsv.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
    const std::string& bench_file() {
        return csv_bench::input_path();
    }

    std::filesystem::path output_path(const char* suffix) {
        return std::filesystem::temp_directory_path() / suffix;
    }

    csv::CSVFormat bench_format() {
        csv::CSVFormat format;
        format.delimiter(',').header_row(0);
        return format;
    }

    rapidcsv::SeparatorParams rapidcsv_separator_params() {
        return rapidcsv::SeparatorParams(',', false, rapidcsv::sPlatformHasCR, true);
    }

    csv::DataFrame<> load_csv_parser_frame(const std::string& path, std::size_t& rows) {
        csv::CSVReader reader(path, bench_format());
        csv::DataFrame<> frame(reader);
        rows = frame.size();
        return frame;
    }

    rapidcsv::Document load_rapidcsv_document(const std::string& path, std::size_t& rows) {
        rapidcsv::Document document(
            path,
            rapidcsv::LabelParams(0, -1),
            rapidcsv_separator_params()
        );
        rows = document.GetRowCount();
        return document;
    }

    template<typename Frame>
    void apply_two_column_edits(Frame& frame) {
        if (frame.n_rows() == 0 || frame.n_cols() < 2) {
            return;
        }

        frame.at(0)[0] = "edited-left";
        frame.at(0)[1] = "edited-right";
    }

    void apply_two_column_edits(rapidcsv::Document& document) {
        if (document.GetRowCount() == 0) {
            return;
        }

        const auto column_names = document.GetColumnNames();
        if (column_names.size() < 2) {
            return;
        }

        document.SetCell<std::string>(column_names[0], 0, "edited-left");
        document.SetCell<std::string>(column_names[1], 0, "edited-right");
    }

    void save_csv_parser_frame(const csv::DataFrame<>& frame, const std::filesystem::path& out_path) {
        std::ofstream output(out_path, std::ios::binary | std::ios::trunc);
        auto writer = csv::make_csv_writer(output).set_auto_flush(false);
        writer << frame.columns();
        for (const auto& row : frame) {
            writer << row;
        }
    }

    void save_rapidcsv_document(rapidcsv::Document& document, const std::filesystem::path& out_path) {
        document.Save(out_path.string());
    }

    void BM_csv_parser_dataframe_load(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            auto frame = load_csv_parser_frame(path, rows);

            benchmark::DoNotOptimize(frame.size());
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_csv_parser_dataframe_save_with_csvwriter(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;
        const auto out_path = output_path("csv_parser_dataframe_save_bench.csv");
        auto frame = load_csv_parser_frame(path, rows);
        apply_two_column_edits(frame);

        for (auto _ : state) {
            save_csv_parser_frame(frame, out_path);

            benchmark::DoNotOptimize(rows);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_csv_parser_dataframe_load_and_csvwriter_save(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;
        const auto out_path = output_path("csv_parser_dataframe_roundtrip_bench.csv");

        for (auto _ : state) {
            auto frame = load_csv_parser_frame(path, rows);
            apply_two_column_edits(frame);
            save_csv_parser_frame(frame, out_path);

            benchmark::DoNotOptimize(rows);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_rapidcsv_document_load(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;

        for (auto _ : state) {
            auto document = load_rapidcsv_document(path, rows);

            benchmark::DoNotOptimize(document.GetRowCount());
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_rapidcsv_document_save(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;
        const auto out_path = output_path("rapidcsv_document_save_bench.csv");
        auto document = load_rapidcsv_document(path, rows);
        apply_two_column_edits(document);

        for (auto _ : state) {
            save_rapidcsv_document(document, out_path);

            benchmark::DoNotOptimize(rows);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    void BM_rapidcsv_document_load_and_save(benchmark::State& state) {
        const auto& path = bench_file();
        const auto bytes = std::filesystem::file_size(path);
        std::size_t rows = 0;
        const auto out_path = output_path("rapidcsv_document_roundtrip_bench.csv");

        for (auto _ : state) {
            auto document = load_rapidcsv_document(path, rows);
            apply_two_column_edits(document);
            save_rapidcsv_document(document, out_path);

            benchmark::DoNotOptimize(rows);
            benchmark::ClobberMemory();
        }

        csv_bench::set_items_processed(state, rows);
        csv_bench::set_bytes_processed(state, bytes);
    }

    BENCHMARK(BM_csv_parser_dataframe_load)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_csv_parser_dataframe_save_with_csvwriter)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_csv_parser_dataframe_load_and_csvwriter_save)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_rapidcsv_document_load)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_rapidcsv_document_save)->UseRealTime()->Unit(benchmark::kMillisecond);
    BENCHMARK(BM_rapidcsv_document_load_and_save)->UseRealTime()->Unit(benchmark::kMillisecond);
}

CSV_BENCHMARK_MAIN()
