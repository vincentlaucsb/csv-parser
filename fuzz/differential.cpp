#include "support.hpp"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!size || size > 65536) return 0;
    csv_fuzz::compare(std::string(reinterpret_cast<const char*>(data + 1), size - 1), csv_fuzz::format(data[0]));
    return 0;
}
