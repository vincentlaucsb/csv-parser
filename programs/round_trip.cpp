#include "csv.hpp"
#include <iostream>

int main(int argc, char** argv) {
    using namespace csv;

    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " [file] [out]" << std::endl;
        exit(1);
    }

    std::string file = argv[1];
    std::string out = argv[2];

    std::ofstream outfile(out);
    auto writer = make_csv_writer(outfile);    

    CSVFormat format;
    format.variable_columns(true);
    CSVReader reader(file, format);
    writer << reader.get_col_names();

    for (auto& row: reader) {
        writer << std::vector<std::string>(row);
    }

    return 0;
}