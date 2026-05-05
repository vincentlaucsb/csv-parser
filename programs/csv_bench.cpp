// Calculate benchmarks for CSV parser

#include "csv.hpp"
#include <chrono>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    using namespace csv;

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " [file]" << std::endl;
        exit(1);
    }

    // Benchmark 1: File IO + Parsing
    std::string filename = argv[1];
    auto start = std::chrono::system_clock::now();
    auto info = get_file_info(filename);
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end - start;

    std::cout << "Parsing took (including disk IO): " << diff.count() << std::endl;
    std::cout << "Dimensions: " << info.n_rows << " rows x " << info.n_cols << " columns " << std::endl;
    if (info.speculative_diagnostics.chunks > 0) {
        std::cout << "Speculative chunks: " << info.speculative_diagnostics.chunks
            << " ambiguous=" << info.speculative_diagnostics.ambiguous_chunks
            << " probability_model=" << info.speculative_diagnostics.probability_model_chunks
            << " size_heuristic=" << info.speculative_diagnostics.record_size_heuristic_chunks
            << " repairs=" << info.speculative_diagnostics.validation_repairs
            << " assumed_quoted=" << info.speculative_diagnostics.assumed_quoted_chunks
            << " assumed_unquoted=" << info.speculative_diagnostics.assumed_unquoted_chunks
            << std::endl;
    }
    std::cout << "Columns: ";
    for (auto& col : info.col_names) {
        std::cout << " " << col;
    }
    std::cout << std::endl;

    // Benchmark 2: Parsing Only
    /*
    std::ifstream csv(filename);
    std::stringstream buffer;
    buffer << csv.rdbuf();

    auto csv_str = buffer.str();

    start = std::chrono::system_clock::now();
    parse(csv_str);
    end = std::chrono::system_clock::now();
    diff = end - start;

    std::cout << "Parsing took: " << diff.count() << std::endl;
    */

    return 0;
}
