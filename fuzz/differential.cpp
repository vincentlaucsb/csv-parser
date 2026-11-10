#include "support.hpp"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < csv_fuzz::control_bytes || size > 65536) return 0;
    csv_fuzz::compare(csv_fuzz::payload(data, size), csv_fuzz::format(data));
    return 0;
}
