#pragma once

#include <glaze/csv.hpp>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace csv_bench {
    struct glaze_row {
        std::string id;
        std::string city;
        std::string state;
        std::string category;
        std::string amount;
        std::string quantity;
        std::string flag;
        std::string note;
    };

    struct glaze_typed_row {
        std::string id;
        std::string city;
        std::string state;
        std::string category;
        std::uint64_t amount;
        std::uint64_t quantity;
        std::string flag;
        std::string note;
    };

    inline constexpr glz::opts_csv glaze_csv_options{
        .layout = glz::rowwise,
        .use_headers = false,
        .skip_header_row = true
    };

    inline std::string read_file_to_string(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Failed to open benchmark input file");
        }

        input.seekg(0, std::ios::end);
        const auto size = input.tellg();
        if (size < 0) {
            throw std::runtime_error("Failed to determine benchmark input size");
        }

        std::string buffer(static_cast<std::size_t>(size), '\0');
        input.seekg(0, std::ios::beg);
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (!input && static_cast<std::size_t>(input.gcount()) != buffer.size()) {
            throw std::runtime_error("Failed to read benchmark input file");
        }

        return buffer;
    }

}
