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

    // Benchmark 2: Parsing Only
    std::ifstream csv(filename);
    std::stringstream buffer;
    buffer << csv.rdbuf();

    auto csv_str = buffer.str();

    start = std::chrono::system_clock::now();
    parse(csv_str);
    end = std::chrono::system_clock::now();
    diff = end - start;

    std::cout << "Parsing took: " << diff.count() << std::endl;

    return 0;
}