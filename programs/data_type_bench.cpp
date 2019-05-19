#include <charconv>
#include <algorithm>
#include "csv.hpp"
#ifndef NDEBUG
#define NDEBUG
#endif

long double get_max(std::string file, std::string column, bool use_std = false);

long double get_max(std::string file, std::string column, bool use_std) {
    using namespace csv;
    long double max = -std::numeric_limits<long double>::infinity();
    CSVReader reader(file);

    for (auto& row : reader) {
        auto field = row[column];
        long double out = 0;

        if (use_std) {
            auto _field = field.get<std::string_view>();
            auto data = _field.data();
            std::from_chars(
                data, data + _field.size(),
                out
            );
        }
        else {
            out = field.get<long double>();
        }

        if (out > max) {
            max = out;
        }
    }

    return max;
}

int main(int argc, char** argv) {
    using namespace csv;

    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " [file] [column]" << std::endl;
        exit(1);
    }

    std::string file = argv[1],
        column = argv[2];

    long double max = 0, std_avg = 0, csv_avg = 0;
    const long double trials = 5;
    

    for (size_t i = 0; i < trials; i++) {
        auto start = std::chrono::system_clock::now();
        max = get_max(file, column, true);
        auto end = std::chrono::system_clock::now();
        std::chrono::duration<double> diff = end - start;
        std_avg += diff.count() / trials;

        start = std::chrono::system_clock::now();
        max = get_max(file, column, false);
        end = std::chrono::system_clock::now();
        diff = end - start;
        csv_avg += diff.count() / trials;
    }

    std::cout << "std::from_chars: " << std_avg << std::endl;
    std::cout << "csv::data_type: " << csv_avg << std::endl;
    std::cout << "Maximum value: " << max << std::endl;

    return 0;
}