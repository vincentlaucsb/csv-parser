#include "support.hpp"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!size || size > 64) return 0;
    // A tiny recipe expands to >10MB, with >=500K distinct-column rows.
    // Position interesting syntax immediately around the real chunk boundary.
    const std::string filler = "12345,abcdef,XYZ12\n";
    const bool compact = size > 2 && (data[2] & 1);
    const size_t boundary = compact ? csv::internals::CSV_CHUNK_SIZE_FLOOR
                                    : csv::internals::CSV_CHUNK_SIZE_DEFAULT;
    // mmap first parses a 500000-byte preview, rewinding its incomplete row;
    // streams fill their first complete window directly. Exercise both offsets.
    const size_t preview_prefix = (500000 / filler.size()) * filler.size();
    const size_t window_end = boundary + ((size > 2 && (data[2] & 2)) ? preview_prefix : 0);
    const int displacement = static_cast<int>(data[0] % 17) - 8;
    const size_t position = static_cast<size_t>(static_cast<int64_t>(window_end) + displacement);
    std::string bytes;
    bytes.reserve(12000000);
    while (bytes.size() + filler.size() + 32 < position) bytes += filler;
    // Legal padding record; place its closing quote at the selected offset.
    bytes += '"';
    bytes.append(position - bytes.size(), 'p');
    static const char* tails[] = {"\",B,C\n", "\"\"x\",B,C\r\n", "\ninside\",B,C\n",
        "\",B,C\r", "\",B,C", "\"unterminated", "\",B,,C\n", "\",B,\0C\n"};
    const size_t tail = (size > 1 ? data[1] : 0) % 8;
    if (tail == 7) bytes.append("\",B,\0C\n", 7);
    else bytes += tails[tail];
    // EOF recipes must end at the syntax under test. Default recipes already
    // contain >500K rows before that point; others add trailing records.
    if (tail != 4 && tail != 5) {
        for (size_t i = 0; i < 128; ++i) bytes += filler;
    }
    csv::CSVFormat config;
    csv::CSVError error;
    csv_fuzz::require(config.try_chunk_size(boundary, error));
    config.no_header().variable_columns(csv::VariableColumnPolicy::KEEP).speculative_parallel_threads(1);
    csv_fuzz::compare(bytes, config);
    return 0;
}
