#include <csv.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "internal/basic_csv_parser.hpp"

namespace {
    bool arg_is(int argc, char** argv, const char* value) {
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == value) {
                return true;
            }
        }

        return false;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: csv_parser_msvc_smoke <csv-file> [iterations] [construct-only|count-only|field-access]\n";
        return 1;
    }

    const std::string path = argv[1];
    const int iterations = argc >= 3 ? std::atoi(argv[2]) : 1000;
    const bool construct_only = arg_is(argc, argv, "construct-only");
    const bool construct_leak = arg_is(argc, argv, "construct-leak");
    const bool count_only = arg_is(argc, argv, "count-only");
    const bool stream_path = arg_is(argc, argv, "stream");
    const bool align_check = arg_is(argc, argv, "align-check");
    const bool simd_check = arg_is(argc, argv, "simd-check");

    if (align_check) {
        void* ptr = ::operator new(sizeof(csv::internals::MmapParser));
        std::cerr << "alignof(SentinelVecs)=" << alignof(csv::internals::SentinelVecs)
                  << " alignof(MmapParser)=" << alignof(csv::internals::MmapParser)
                  << " ptr_mod_32=" << (reinterpret_cast<std::uintptr_t>(ptr) % 32) << '\n';
        ::operator delete(ptr);
        return 0;
    }

    if (simd_check) {
        csv::internals::SentinelVecs sentinels(',', '"');
        std::string data(128, 'x');
        data[97] = ',';
        std::cerr << "simd_pos=" << csv::internals::find_next_non_special(data, 0, sentinels)
#ifdef __AVX2__
                  << " __AVX2__=1"
#else
                  << " __AVX2__=0"
#endif
#ifdef __SSE2__
                  << " __SSE2__=1"
#else
                  << " __SSE2__=0"
#endif
#ifdef _M_AVX
                  << " _M_AVX=" << _M_AVX
#else
                  << " _M_AVX=0"
#endif
#ifdef _M_X64
                  << " _M_X64=1"
#else
                  << " _M_X64=0"
#endif
                  << '\n';
        return 0;
    }

    csv::CSVFormat format;
    format.delimiter(',').header_row(0);

    std::uint64_t checksum = 0;
    std::size_t total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        std::ifstream stream;
        std::unique_ptr<csv::CSVReader> leaked_reader;

        if (stream_path) {
            stream.open(path, std::ios::binary);
            leaked_reader.reset(new csv::CSVReader(stream, format));
        } else {
            leaked_reader.reset(new csv::CSVReader(path, format));
        }

        csv::CSVReader& reader = *leaked_reader;

        if (construct_leak) {
            static_cast<void>(leaked_reader.release());
            continue;
        }

        if (construct_only) {
            continue;
        }

        for (auto& row : reader) {
            if (!count_only) {
                auto id = row["id"].get<csv::string_view>();
                auto city = row["city"].get<csv::string_view>();
                auto note = row["note"].get<csv::string_view>();

                checksum += id.size() + city.size() + note.size();
                if (!id.empty()) checksum += static_cast<unsigned char>(id[0]);
                if (!note.empty()) checksum += static_cast<unsigned char>(note[0]);
            }
            ++total_rows;
        }
    }

    std::cerr << "iterations=" << iterations
              << " total_rows=" << total_rows
              << " checksum=" << checksum << '\n';

    return 0;
}
