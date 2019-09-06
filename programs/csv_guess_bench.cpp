// Calculate benchmarks for CSV guessing

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

    std::string filename = argv[1];
    std::vector<double> times = {};
    int trials = 5;

    for (int i = 0; i < trials; i++) {
        auto start = std::chrono::system_clock::now();

        // This reads just the first 500 kb of a file
        CSVReader reader(filename, CSVFormat::guess_csv());

        auto end = std::chrono::system_clock::now();
        std::chrono::duration<double> diff = end - start;
        times.push_back(diff.count());
    }

    double avg = 0;
    for (double time: times) {
        avg += time * 1/trials;
    }
    std::cout << "Guessing took: " << avg << " seconds (averaged over " << trials << " trials)" << std::endl;

    return 0;
}