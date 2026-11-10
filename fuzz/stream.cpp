#include "support.hpp"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!size || size > 65536) return 0;
    auto config = csv_fuzz::format(data[0]);
    config.threading(false);
    (void)csv_fuzz::stream(std::string(reinterpret_cast<const char*>(data + 1), size - 1), config, false);
    return 0;
}
