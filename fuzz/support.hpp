#pragma once
#include "csv.hpp"
#include "shared/file_guard.hpp"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace csv_fuzz {
inline void require(bool ok) { if (!ok) std::abort(); }
// One file per process, reused across persistent libFuzzer iterations. Workers
// finish and mappings close before the next rewrite; FileGuard owns cleanup.
inline FileGuard& input_file() {
    static FileGuard file((std::filesystem::temp_directory_path() /
        ("csv-fuzz-" + std::to_string(getpid()) + ".csv")).string());
    return file;
}
// Two control bytes precede the CSV payload so every option varies independently.
// control: bit 0 guess delimiter, else bits 1-2 pick one; bit 3 no header, else
// bit 4 is the header row; bit 5 disables quoting; bit 6 trims ' ' and '\r'.
// policy: bits 0-1 select the variable-column policy (THROW..KEEP_NON_EMPTY).
constexpr size_t control_bytes = 2;
inline csv::CSVFormat format(uint8_t control, uint8_t policy) {
    csv::CSVFormat result;
    csv::CSVError error;
    if (control & 1) require(result.try_delimiter(std::vector<char>{',', ';', '\t', '|'}, error));
    else require(result.try_delimiter(",;\t|"[(control >> 1) & 3], error));
    if (control & 8) result.no_header();
    else result.header_row((control >> 4) & 1);
    result.quote((control & 32) == 0);
    if (control & 64) require(result.try_trim({' ', '\r'}, error));
    result.variable_columns(static_cast<csv::VariableColumnPolicy>(static_cast<int>(policy & 3) - 1));
    return result;
}
inline csv::CSVFormat format(const uint8_t* data) { return format(data[0], data[1]); }
inline std::string payload(const uint8_t* data, size_t size) {
    return std::string(reinterpret_cast<const char*>(data + control_bytes), size - control_bytes);
}
struct Result {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> names;
    std::vector<char> delimiters, trim;
    size_t count = 0;
    int header = 0;
    char quote = 0;
    bool quoting = false;
    csv::CSVErrorCode error = csv::CSVErrorCode::None;
    bool operator==(const Result&) const = default;
};
inline Result consume(std::unique_ptr<csv::CSVReader> reader, csv::CSVError error,
                      bool scheduling) {
    Result out;
    if (reader) {
        csv::CSVRow row;
        while (reader->try_read_row(row, error)) {
            std::vector<std::string> fields;
            fields.reserve(row.size());
            for (auto field : row) {
                fields.emplace_back(field.get_sv());
                (void)field.type();
                csv::CSVConversionError conversion_error;
                int64_t integer = 0;
                uint64_t natural = 0;
                double real = 0;
                bool boolean = false;
                (void)field.try_get(integer, conversion_error);
                (void)field.try_get(natural, conversion_error);
                (void)field.try_get(real, conversion_error);
                (void)field.try_get(boolean, conversion_error);
                require(fields.back() == field.get_sv());
            }
            out.rows.push_back(std::move(fields));
            if (scheduling && (out.rows.size() % 127 == 0)) std::this_thread::yield();
        }
        // Move construction joins even when a consumer-side row-policy error
        // stopped reading early. Inspect terminal status only after that join.
        reader = std::make_unique<csv::CSVReader>(std::move(*reader));
        require(!reader->try_read_row(row, error));
        const auto resolved = reader->get_format();
        out.names = reader->get_col_names();
        out.delimiters = resolved.get_possible_delims();
        out.trim = resolved.get_trim_chars();
        out.header = resolved.get_header();
        out.quote = resolved.get_quote_char();
        out.quoting = resolved.is_quoting_enabled();
        out.count = reader->n_rows();
        reader.reset();
    }
    out.error = error.code;
    return out;
}
inline Result stream(const std::string& bytes, csv::CSVFormat config, bool scheduling) {
    std::istringstream source(bytes);
    csv::CSVError error;
    auto reader = csv::CSVReader::try_open(source, error, config);
    return consume(std::move(reader), error, scheduling);
}
inline void equal(const Result& expected, const Result& actual, const char* backend, bool threaded) {
    if (expected == actual) return;
    std::fprintf(stderr, "CSV differential mismatch: backend=%s threaded=%d rows=%zu/%zu count=%zu/%zu error=%d/%d\n",
        backend, static_cast<int>(threaded), expected.rows.size(), actual.rows.size(),
        expected.count, actual.count, static_cast<int>(expected.error), static_cast<int>(actual.error));
    std::abort();
}
inline void compare(const std::string& bytes, csv::CSVFormat config) {
    auto& file = input_file();
    {
        std::ofstream output(file.filename, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        require(bool(output));
    }
    config.threading(false);
    const auto baseline = stream(bytes, config, false);
    for (bool threaded : {false, true}) {
        config.threading(threaded);
        equal(baseline, stream(bytes, config, threaded), "stream", threaded);
        csv::CSVError error;
        auto reader = csv::CSVReader::try_open(file.filename, error, config);
        equal(baseline, consume(std::move(reader), error, threaded), "mmap", threaded);
    }
}
}
