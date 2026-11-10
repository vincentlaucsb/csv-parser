#include "support.hpp"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < csv_fuzz::control_bytes || size > 65536) return 0;
    auto config = csv_fuzz::format(data);
    config.threading(false);
    (void)csv_fuzz::stream(csv_fuzz::payload(data, size), config, false);
    return 0;
}
