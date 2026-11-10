#include "support.hpp"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!size || size > 64) return 0;
    // A tiny recipe expands to >10MB, with >=500K distinct-column rows.
    // Position interesting syntax immediately around the real chunk boundary.
    const std::string filler = "12345,abcdef,XYZ12\n";
    const uint8_t flags = size > 2 ? data[2] : 0;
    const bool compact = flags & 1;
    const bool custom = flags & 4;      // bytes 3.. replace the fixed tail
    const bool unquoted = flags & 8;    // boundary lands inside an unquoted field
    const size_t boundary = compact ? csv::internals::CSV_CHUNK_SIZE_FLOOR
                                    : csv::internals::CSV_CHUNK_SIZE_DEFAULT;
    // mmap first parses a head preview, rewinding its incomplete row; streams
    // fill their first complete window directly. Exercise both offsets.
    const size_t preview_prefix = (csv::internals::CSV_HEAD_PREVIEW_SIZE / filler.size()) * filler.size();
    const size_t window_end = boundary + ((flags & 2) ? preview_prefix : 0);
    const int displacement = static_cast<int>(data[0] % 17) - 8;
    const size_t position = static_cast<size_t>(static_cast<int64_t>(window_end) + displacement);
    std::string bytes;
    bytes.reserve(12000000);
    while (bytes.size() + filler.size() + 32 < position) bytes += filler;
    // Legal padding record; the tail's first byte lands at the selected offset.
    if (!unquoted) bytes += '"';
    bytes.append(position - bytes.size(), 'p');
    static const char* tails[] = {"\",B,C\n", "\"\"x\",B,C\r\n", "\ninside\",B,C\n",
        "\",B,C\r", "\",B,C", "\"unterminated", "\",B,,C\n", "\",B,\0C\n"};
    const size_t tail = (size > 1 ? data[1] : 0) % 8;
    if (custom) bytes.append(reinterpret_cast<const char*>(data) + 3, size > 3 ? size - 3 : 0);
    else if (tail == 7) bytes.append("\",B,\0C\n", 7);
    else bytes += tails[tail];
    // EOF recipes must end at the syntax under test. Default recipes already
    // contain >500K rows before that point; others add trailing records.
    const bool eof = (flags & 16) || (!custom && (tail == 4 || tail == 5));
    if (!eof) {
        for (size_t i = 0; i < 128; ++i) bytes += filler;
    }
    csv::CSVFormat config;
    csv::CSVError error;
    csv_fuzz::require(config.try_chunk_size(boundary, error));
    config.no_header().variable_columns(csv::VariableColumnPolicy::KEEP).speculative_parallel_threads(1);
    csv_fuzz::compare(bytes, config);
    return 0;
}
