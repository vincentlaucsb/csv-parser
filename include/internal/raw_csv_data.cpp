#include "raw_csv_data.hpp"

namespace csv {
    bool BasicCSVParser::parse(
        internals::ParseFlagMap _parse_flags,
        internals::WhitespaceMap _ws_flags,
        bool& quote_escape,
        std::deque<RawCSVRow>& records
    ) {
        using internals::ParseFlags;

        // Optimizations
        auto* HEDLEY_RESTRICT parse_flags = _parse_flags.data();
        auto* HEDLEY_RESTRICT ws_flags = _ws_flags.data();
        auto in = this->get_string();

        // Parser state
        this->current_row = RawCSVRow(this->raw_data);

        // TODO: End of fragment handling
        for (size_t i = 0; i < in.size(); ) {
            switch (parse_flags[in[i] + 128]) {
            case ParseFlags::NOT_SPECIAL:
                this->raw_data->fields.push_back(
                    this->parse_field(in, parse_flags, i)
                );
                break;

            case ParseFlags::QUOTE:
                quote_escape = true;
                this->raw_data->fields.push_back(
                    this->parse_quoted_field(in, parse_flags, i, quote_escape)
                );

                break;

            case ParseFlags::DELIMITER:
                current_row.row_length++;
                i++;
                break;

            default: // Newline
                // End of record -> Write record
                current_row.row_length++;
                i++;

                // Catches CRLF (or LFLF)
                if (i < in.size() && in[i] == '\n')
                    i++;

                records.push_back(std::move(current_row));
                current_row = RawCSVRow(this->raw_data);
                current_row.data_start = this->raw_data->fields.size();
                break;
            }
        }

        // Return True if we are done parsing and there are no
        // incomplete rows or fields
        return this->current_row.row_length == 0;
    }

    RawCSVField BasicCSVParser::parse_quoted_field(
        csv::string_view in, internals::ParseFlags parse_flags[], size_t& i,
        bool& quote_escape) {
        using internals::ParseFlags;
        bool has_double_quote = false;
        size_t start = i;
        size_t length = 0;

        for (; i < in.size(); i++) {
            if (parse_flags[in[i] + 128] == ParseFlags::QUOTE) {
                if (i + 1 < in.size()) {
                    auto next_ch = parse_flags[in[i + 1] + 128];
                    if (next_ch >= ParseFlags::DELIMITER) {
                        // Case: Delim or newline => end of field
                        quote_escape = false;

                        return {
                            start,
                            length,
                            has_double_quote
                        };
                    }
                    else if (next_ch == ParseFlags::QUOTE) {
                        // Case: Escaped quote
                        has_double_quote = true;
                        ++i;
                    }

                    // Fallthrough
                }
            }

            length++;
        }

        // Reached end of CSV fragment before completing
        // => rewind to start of field
        i = start;
    }

    RawCSVField BasicCSVParser::parse_field(csv::string_view in, internals::ParseFlags parse_flags[], size_t& i) {
        using internals::ParseFlags;

        size_t start = i;
        size_t length = 0;

        // We'll allow unescaped quotes...
        for (; i < in.size() && parse_flags[in[i] + 128] <= ParseFlags::QUOTE; i++) {
            length++;
        }

        return { start, length, false };
    }

    std::string RawCSVRow::get_field(size_t index)
    {
        RawCSVField raw_field = this->data->fields[
            this->data_start + index
        ];

        csv::string_view csv_field = this->data->get_string().substr(
            raw_field.start,
            raw_field.length
        );

        if (raw_field.has_doubled_quote) {
            std::string ret = "";
            for (size_t i = 0; i < csv_field.size(); i++) {
                // TODO: Use parse flags
                if (csv_field[i] == '"') {
                    if (i + 1 < csv_field.size() && csv_field[i + 1] == '"') {
                        i++;
                        continue;
                    }

                    ret += csv_field[i];
                }
            }

            return ret;
        }

        return std::string(csv_field);
    }
}