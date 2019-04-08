#include <charconv>
#include <algorithm>
#include "csv.hpp"
#ifndef NDEBUG
#define NDEBUG
#endif

int main(int argc, char** argv) {
    using namespace csv;

    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " [file] [column]" << std::endl;
        exit(1);
    }

    std::string file = argv[1],
        column = argv[2];

    bool use_std = false;

    if (argc == 4) {
        use_std = true;
    }

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

    std::cout << "Maximum value: " << max << std::endl;

    return 0;
}