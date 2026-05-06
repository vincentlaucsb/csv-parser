// Explore csv-parser chunk size and speculative worker tuning.

#include "csv.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    struct RunConfig {
        size_t chunk_size = csv::internals::CSV_CHUNK_SIZE_DEFAULT;
        size_t threads = 0;
    };

    struct RunResult {
        RunConfig config;
        double seconds = 0;
        double mib_per_second = 0;
        size_t rows = 0;
        size_t columns = 0;
        size_t parser_threads = 1;
        csv::internals::SpeculativeParseDiagnostics diagnostics;
    };

    void print_usage(const char* program) {
        std::cout
            << "Usage: " << program << " <file> [options]\n"
            << "\n"
            << "Options:\n"
            << "  --chunks <list>      Comma-separated chunk sizes, e.g. 2M,4M,8M,10M,16M\n"
            << "  --threads <list>     Comma-separated worker counts; 0 means auto\n"
            << "  --passes <n>         Repeated runs for each configuration (default: 1)\n"
            << "  --batch-rows <n>     Rows drained per read_chunk() call (default: 50000)\n"
            << "  --no-speculative     Disable speculative parallel parsing\n"
            << "\n"
            << "Size suffixes use binary units: K=1024, M=1024^2, G=1024^3.\n";
    }

    std::string lower(std::string value) {
        for (size_t i = 0; i < value.size(); ++i) {
            value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
        }
        return value;
    }

    size_t parse_size(std::string text) {
        text = lower(text);
        if (text.empty()) {
            throw std::invalid_argument("empty size");
        }

        size_t multiplier = 1;
        const char suffix = text.back();
        if (suffix == 'k' || suffix == 'm' || suffix == 'g') {
            text.pop_back();
            if (suffix == 'k') {
                multiplier = 1024;
            }
            else if (suffix == 'm') {
                multiplier = 1024 * 1024;
            }
            else {
                multiplier = 1024 * 1024 * 1024;
            }
        }

        char* end = nullptr;
        const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0') {
            throw std::invalid_argument("invalid size: " + text);
        }

        return static_cast<size_t>(value) * multiplier;
    }

    std::vector<size_t> parse_size_list(const std::string& text) {
        std::vector<size_t> values;
        std::stringstream stream(text);
        std::string item;
        while (std::getline(stream, item, ',')) {
            values.push_back(parse_size(item));
        }
        return values;
    }

    std::vector<size_t> parse_count_list(const std::string& text) {
        std::vector<size_t> values;
        std::stringstream stream(text);
        std::string item;
        while (std::getline(stream, item, ',')) {
            values.push_back(parse_size(item));
        }
        return values;
    }

    size_t file_size_bytes(const std::string& filename) {
        std::ifstream file(filename.c_str(), std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("Cannot open file " + filename);
        }

        const std::ifstream::pos_type size = file.tellg();
        if (size < 0) {
            throw std::runtime_error("Cannot determine file size " + filename);
        }

        return static_cast<size_t>(size);
    }

    RunResult run_once(
        const std::string& filename,
        size_t file_size,
        const RunConfig& config,
        bool speculative,
        size_t batch_rows
    ) {
        csv::CSVFormat format = csv::CSVFormat::guess_csv();
        format.chunk_size(config.chunk_size);

        if (speculative) {
            format.speculative_parallel()
                .speculative_parallel_min_bytes(1)
                .speculative_parallel_threads(config.threads);
        }
        else {
            format.speculative_parallel(false);
        }

        const auto start = std::chrono::steady_clock::now();
        csv::CSVReader reader(filename, format);
        std::vector<csv::CSVRow> rows;
        while (reader.read_chunk(rows, batch_rows)) {}
        const auto end = std::chrono::steady_clock::now();

        const std::chrono::duration<double> elapsed = end - start;

        RunResult result;
        result.config = config;
        result.seconds = elapsed.count();
        result.mib_per_second = result.seconds > 0
            ? static_cast<double>(file_size) / (1024.0 * 1024.0) / result.seconds
            : 0;
        result.rows = reader.n_rows();
        result.columns = reader.get_col_names().size();
        result.parser_threads = reader.parse_worker_count();
        result.diagnostics = reader.speculative_diagnostics();
        return result;
    }

    void print_result_header() {
        std::cout
            << "chunk_bytes,requested_threads,parser_threads,seconds,MiB_per_s,"
            << "rows,columns,spec_chunks,ambiguous,probability_model,"
            << "size_heuristic,repairs,assumed_quoted,assumed_unquoted\n";
    }

    void print_result(const RunResult& result) {
        std::cout
            << result.config.chunk_size << ','
            << result.config.threads << ','
            << result.parser_threads << ','
            << std::fixed << std::setprecision(6) << result.seconds << ','
            << std::fixed << std::setprecision(2) << result.mib_per_second << ','
            << result.rows << ','
            << result.columns << ','
            << result.diagnostics.chunks << ','
            << result.diagnostics.ambiguous_chunks << ','
            << result.diagnostics.probability_model_chunks << ','
            << result.diagnostics.record_size_heuristic_chunks << ','
            << result.diagnostics.validation_repairs << ','
            << result.diagnostics.assumed_quoted_chunks << ','
            << result.diagnostics.assumed_unquoted_chunks
            << '\n';
    }
}

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
        print_usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    std::string filename;
    std::vector<size_t> chunk_sizes;
    std::vector<size_t> thread_counts;
    size_t passes = 1;
    size_t batch_rows = 50000;
    bool speculative = true;

    chunk_sizes.push_back(2 * 1024 * 1024);
    chunk_sizes.push_back(4 * 1024 * 1024);
    chunk_sizes.push_back(8 * 1024 * 1024);
    chunk_sizes.push_back(csv::internals::CSV_CHUNK_SIZE_DEFAULT);
    chunk_sizes.push_back(16 * 1024 * 1024);
    chunk_sizes.push_back(32 * 1024 * 1024);

    thread_counts.push_back(1);
    thread_counts.push_back(2);
    thread_counts.push_back(4);
    thread_counts.push_back(8);
    thread_counts.push_back(0);

    try {
        filename = argv[1];

        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--chunks" && i + 1 < argc) {
                chunk_sizes = parse_size_list(argv[++i]);
            }
            else if (arg == "--threads" && i + 1 < argc) {
                thread_counts = parse_count_list(argv[++i]);
            }
            else if (arg == "--passes" && i + 1 < argc) {
                passes = parse_size(argv[++i]);
            }
            else if (arg == "--batch-rows" && i + 1 < argc) {
                batch_rows = parse_size(argv[++i]);
            }
            else if (arg == "--no-speculative") {
                speculative = false;
            }
            else if (arg == "--help" || arg == "-h") {
                print_usage(argv[0]);
                return 0;
            }
            else {
                throw std::invalid_argument("unknown or incomplete option: " + arg);
            }
        }

        if (passes == 0) {
            throw std::invalid_argument("--passes must be greater than zero");
        }
        if (batch_rows == 0) {
            throw std::invalid_argument("--batch-rows must be greater than zero");
        }

        chunk_sizes.erase(std::remove(chunk_sizes.begin(), chunk_sizes.end(), 0), chunk_sizes.end());
        thread_counts.erase(std::unique(thread_counts.begin(), thread_counts.end()), thread_counts.end());
        if (chunk_sizes.empty()) {
            throw std::invalid_argument("at least one non-zero chunk size is required");
        }
        if (thread_counts.empty()) {
            throw std::invalid_argument("at least one thread count is required");
        }
        for (size_t i = 0; i < chunk_sizes.size(); ++i) {
            if (chunk_sizes[i] < csv::internals::CSV_CHUNK_SIZE_FLOOR) {
                throw std::invalid_argument("chunk size is below the parser minimum");
            }
        }

        const size_t file_size = file_size_bytes(filename);
        std::cout << "file=" << filename
            << " bytes=" << file_size
            << " speculative=" << (speculative ? "on" : "off")
            << " passes=" << passes
            << '\n';

        print_result_header();
        for (size_t pass = 0; pass < passes; ++pass) {
            for (size_t chunk_i = 0; chunk_i < chunk_sizes.size(); ++chunk_i) {
                for (size_t thread_i = 0; thread_i < thread_counts.size(); ++thread_i) {
                    RunConfig config;
                    config.chunk_size = chunk_sizes[chunk_i];
                    config.threads = thread_counts[thread_i];
                    print_result(run_once(filename, file_size, config, speculative, batch_rows));
                }
            }
        }
    }
    catch (const std::exception& error) {
        std::cerr << "csv_tuning: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
