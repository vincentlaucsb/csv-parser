#include "my_header.hpp"

int main(int argc, char** argv) {
    using namespace csv;

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " [file]" << std::endl;
        exit(1);
    }

    std::string filename = argv[1];
    auto dtypes = csv_data_types(filename);

    for (const auto& entry : dtypes) {
        std::cout << entry.first << ": " << static_cast<int>(entry.second) << std::endl;
    }

    return 0;
}
