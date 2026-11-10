#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t);
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::ifstream input(argv[i], std::ios::binary);
        if (!input) return 2;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    }
}
