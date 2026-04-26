#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace csv_bench {
    inline std::uint64_t parse_u64(std::string_view text) {
        std::uint64_t value = 0;
        const char* begin = text.data();
        const char* end = begin + text.size();
        const auto result = std::from_chars(begin, end, value);

        if (result.ec != std::errc() || result.ptr != end) {
            throw std::runtime_error("Failed to parse unsigned integer field during ETL benchmark.");
        }

        return value;
    }

    template<typename Rows, typename GetField>
    std::uint64_t run_multi_pass_etl(const Rows& rows, GetField&& get_field) {
        std::uint64_t amount_sum = 0;
        for (const auto& row : rows) {
            amount_sum += parse_u64(get_field(row, 4));
        }

        std::uint64_t quantity_sum = 0;
        std::uint64_t enabled_count = 0;
        for (const auto& row : rows) {
            quantity_sum += parse_u64(get_field(row, 5));
            enabled_count += get_field(row, 6) == std::string_view("Y") ? 1u : 0u;
        }

        std::unordered_map<std::string_view, std::uint64_t> category_counts;
        category_counts.reserve(8);
        for (const auto& row : rows) {
            ++category_counts[get_field(row, 3)];
        }

        std::uint64_t text_checksum = 0;
        for (const auto& row : rows) {
            const auto city = get_field(row, 1);
            const auto note = get_field(row, 7);
            text_checksum += static_cast<std::uint64_t>(city.size() * 3 + note.size());
            if (!city.empty()) {
                text_checksum += static_cast<unsigned char>(city.front());
            }
            if (!note.empty()) {
                text_checksum += static_cast<unsigned char>(note.front());
            }
        }

        std::uint64_t category_checksum = 0;
        for (const auto& entry : category_counts) {
            category_checksum += static_cast<std::uint64_t>(entry.first.size()) * entry.second;
        }

        return amount_sum + quantity_sum + enabled_count + text_checksum + category_checksum;
    }
}
