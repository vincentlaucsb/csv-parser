#include <csv.hpp>

int main(int argc, char* argv[])
{
    try {
        using namespace csv;
        CSVReader reader("time-result.csv");

        std::vector<size_t> my_vec;

        std::cout << "Vector max size - " << my_vec.max_size() << std::endl;

        for (CSVRow& row : reader) { // Input iterator
            auto i = 0;
            for (CSVField& field : row) {
                //if (i > 7003) // 7003th is one of the wrongly parsed fields, there are more
                std::cout << i << " - " << field.get<>() << std::endl;
                ++i;
            }
        }
    }
    catch (std::runtime_error& err) {
        std::cout << err.what() << std::endl;
    }
}