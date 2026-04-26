/** @file
 *  @brief Internal JSON serialization helpers for row-like CSV data.
 */

#pragma once

#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "data_type.hpp"

namespace csv {
    namespace internals {
        /** Escape sequences for ASCII control characters that must be encoded in JSON. */
        static const char* const json_control_escape_sequences[32] = {
            "\\u0000", "\\u0001", "\\u0002", "\\u0003",
            "\\u0004", "\\u0005", "\\u0006", "\\u0007",
            "\\b",     "\\t",     "\\n",     "\\u000b",
            "\\f",     "\\r",     "\\u000e", "\\u000f",
            "\\u0010", "\\u0011", "\\u0012", "\\u0013",
            "\\u0014", "\\u0015", "\\u0016", "\\u0017",
            "\\u0018", "\\u0019", "\\u001a", "\\u001b",
            "\\u001c", "\\u001d", "\\u001e", "\\u001f"
        };

        static const char* const json_quote_escape_sequence = "\\\"";
        static const char* const json_backslash_escape_sequence = "\\\\";

        CSV_CONST inline const char* json_escape_sequence(unsigned char c) noexcept {
            if (c < 0x20) {
                return json_control_escape_sequences[c];
            }

            if (c == '"') {
                return json_quote_escape_sequence;
            }

            if (c == '\\') {
                return json_backslash_escape_sequence;
            }

            return nullptr;
        }

        inline std::size_t json_extra_space(csv::string_view s) noexcept {
            std::size_t result = 0;

            for (csv::string_view::size_type i = 0; i < s.size(); ++i) {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                const char* escape_sequence = json_escape_sequence(c);
                if (escape_sequence) {
                    result += std::strlen(escape_sequence) - 1;
                }
            }

            return result;
        }

        inline void append_json_escaped(std::string& out, csv::string_view s) noexcept {
            const std::size_t extra = json_extra_space(s);
            if (extra == 0) {
                out.append(s.data(), s.size());
                return;
            }

            const std::size_t original_size = out.size();
            out.resize(original_size + s.size() + extra);
            char* dest = &out[original_size];
            std::size_t pos = 0;

            for (csv::string_view::size_type i = 0; i < s.size(); ++i) {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                const char* escape_sequence = json_escape_sequence(c);
                if (escape_sequence) {
                    const std::size_t escape_length = std::strlen(escape_sequence);
                    std::memcpy(dest + pos, escape_sequence, escape_length);
                    pos += escape_length;
                } else {
                    dest[pos++] = static_cast<char>(c);
                }
            }
        }

        inline std::string json_escape_string(csv::string_view s) noexcept {
            std::string out;
            out.reserve(s.size() + json_extra_space(s));
            append_json_escaped(out, s);
            return out;
        }

        class JsonConverter {
        public:
            JsonConverter() = default;
            explicit JsonConverter(const std::vector<std::string>& column_names)
                : column_names_(column_names) {
                escaped_object_keys_.reserve(column_names_.size());
                column_positions_.reserve(column_names_.size());

                for (size_t i = 0; i < column_names_.size(); ++i) {
                    escaped_object_keys_.push_back('"' + json_escape_string(column_names_[i]) + "\":");
                    column_positions_[column_names_[i]] = i;
                }
            }

            template<typename FieldAt>
            std::string row_to_json(size_t field_count, FieldAt field_at, const std::vector<std::string>& subset = {}) const {
                return subset.empty()
                    ? this->row_to_json_all(field_count, field_at)
                    : this->row_to_json_subset(field_count, field_at, subset);
            }

            template<typename FieldAt>
            std::string row_to_json_array(size_t field_count, FieldAt field_at, const std::vector<std::string>& subset = {}) const {
                return subset.empty()
                    ? this->row_to_json_array_all(field_count, field_at)
                    : this->row_to_json_array_subset(field_count, field_at, subset);
            }

        private:
            template<typename FieldAt>
            std::string row_to_json_all(size_t field_count, FieldAt field_at) const {
                const size_t count = (field_count < escaped_object_keys_.size()) ? field_count : escaped_object_keys_.size();
                std::string out = "{";

                for (size_t i = 0; i < count; ++i) {
                    out += escaped_object_keys_[i];
                    this->append_json_value(out, field_at(i));
                    if (i + 1 < count) {
                        out += ',';
                    }
                }

                out += '}';
                return out;
            }

            template<typename FieldAt>
            std::string row_to_json_subset(
                size_t field_count,
                FieldAt field_at,
                const std::vector<std::string>& subset
            ) const {
                std::string out = "{";
                bool first = true;

                for (const auto& column : subset) {
                    const size_t index = this->index_of(column);
                    if (index >= field_count) {
                        continue;
                    }

                    if (!first) {
                        out += ',';
                    }

                    out += escaped_object_keys_[index];
                    this->append_json_value(out, field_at(index));
                    first = false;
                }

                out += '}';
                return out;
            }

            template<typename FieldAt>
            std::string row_to_json_array_all(size_t field_count, FieldAt field_at) const {
                const size_t count = (field_count < column_names_.size()) ? field_count : column_names_.size();
                std::string out = "[";

                for (size_t i = 0; i < count; ++i) {
                    this->append_json_value(out, field_at(i));
                    if (i + 1 < count) {
                        out += ',';
                    }
                }

                out += ']';
                return out;
            }

            template<typename FieldAt>
            std::string row_to_json_array_subset(
                size_t field_count,
                FieldAt field_at,
                const std::vector<std::string>& subset
            ) const {
                std::string out = "[";
                bool first = true;

                for (const auto& column : subset) {
                    const size_t index = this->index_of(column);
                    if (index >= field_count) {
                        continue;
                    }

                    if (!first) {
                        out += ',';
                    }

                    this->append_json_value(out, field_at(index));
                    first = false;
                }

                out += ']';
                return out;
            }

            void append_json_value(std::string& out, csv::string_view value) const {
                if (internals::data_type(value) >= DataType::CSV_INT8) {
                    out.append(value.data(), value.size());
                } else {
                    out += '"';
                    append_json_escaped(out, value);
                    out += '"';
                }
            }

            size_t index_of(const std::string& column) const {
                const auto it = column_positions_.find(column);
                if (it == column_positions_.end()) {
                    throw std::runtime_error("Can't find a column named " + column);
                }

                return it->second;
            }

            std::vector<std::string> column_names_;
            std::vector<std::string> escaped_object_keys_;
            std::unordered_map<std::string, size_t> column_positions_;
        };
    }
}
