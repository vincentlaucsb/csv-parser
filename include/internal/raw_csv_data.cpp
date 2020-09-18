#include "raw_csv_data.hpp"

namespace csv {
    bool BasicCSVParser::parse(
        csv::string_view in,
        internals::ParseFlagMap _parse_flags,
        internals::WhitespaceMap _ws_flags,
        std::deque<RawCSVRow>& records
    ) {
        using internals::ParseFlags;

        size_t part_of_previous_fragment = 0;

        // Check for previous fragments
        if (this->current_row.row_length > 0) {
            // Make a separate data buffer for the fragment row
            auto temp_str = this->current_row.get_raw_data();
            auto temp_fields = this->current_row.get_raw_fields();
            auto temp_row_length = this->current_row.row_length;

            this->current_row.data = std::make_shared<RawCSVData>();
            auto& fragment_data = this->current_row.data;
            fragment_data->data = temp_str;
            fragment_data->fields = temp_fields;

            ParseLoopData stitch_data;
            stitch_data.in = in;
            stitch_data.parse_flags = _parse_flags;
            stitch_data.ws_flags = _ws_flags;
            stitch_data.raw_data = fragment_data;
            stitch_data.records = &records;
            stitch_data.start_offset = this->current_row.data->data.length();

            // Parse until newline
            this->current_row = RawCSVRow(stitch_data.raw_data);
            this->current_row.data_start = 0;
            this->current_row.row_length = temp_row_length;

            size_t new_stuff_length = this->parse_loop(stitch_data);

            // Copy data over
            fragment_data->data += in.substr(0, new_stuff_length);

            // Merge pre-stitch fields (if necessary)
            if ((fragment_data->fields[temp_row_length - 1].start
                + fragment_data->fields[temp_row_length - 1].length)
                == fragment_data->fields[temp_row_length].start) {
                fragment_data->fields[temp_row_length - 1].length +=
                    fragment_data->fields[temp_row_length].length;

                std::vector<RawCSVField> new_fields = {};
                for (size_t i = 0; i < this->current_row.row_length - 1; i++) {
                    if (i != temp_row_length) {
                        new_fields.push_back(fragment_data->fields[i]);
                    }
                }
            }

            part_of_previous_fragment = new_stuff_length;

        }

        // Local parser state
        in.remove_prefix(part_of_previous_fragment);

        ParseLoopData main_loop_data;
        main_loop_data.in = in;
        main_loop_data.parse_flags = _parse_flags;
        main_loop_data.ws_flags = _ws_flags;
        main_loop_data.raw_data = std::make_shared<RawCSVData>();
        main_loop_data.raw_data->data.assign(in.data(), in.size());
        main_loop_data.records = &records;

        this->current_row = RawCSVRow(main_loop_data.raw_data);

        this->parse_loop(main_loop_data);

        // Return True if we are done parsing and there are no
        // incomplete rows or fields
        return this->current_row.row_length == 0;
    }

    RawCSVField BasicCSVParser::parse_quoted_field(
        csv::string_view in, internals::ParseFlags parse_flags[],
        size_t row_start,
        size_t& i,
        bool& quote_escape) {
        using internals::ParseFlags;
        bool has_double_quote = false;
        size_t start = i - row_start;
        size_t length = 0;

        for (; i < in.size(); i++) {
            if (parse_flags[in[i] + 128] == ParseFlags::QUOTE) {
                if (i + 1 < in.size()) {
                    auto next_ch = parse_flags[in[i + 1] + 128];
                    if (next_ch >= ParseFlags::DELIMITER) {
                        // Case: Delim or newline => end of field
                        quote_escape = false;
                        i++;
                        break;
                    }
                    else if (next_ch == ParseFlags::QUOTE) {
                        // Case: Escaped quote
                        has_double_quote = true;
                        i++;
                    }

                    // Fallthrough
                }
            }

            length++;
        }

        return { start, length, has_double_quote };
    }

    RawCSVField BasicCSVParser::parse_field(csv::string_view in, internals::ParseFlags parse_flags[], size_t row_start, size_t& i) {
        using internals::ParseFlags;

        size_t start = i - row_start;
        size_t length = 0;

        // We'll allow unescaped quotes...
        for (; i < in.size() && parse_flags[in[i] + 128] <= ParseFlags::QUOTE; i++) {
            length++;
        }

        return { start, length, false };
    }

    size_t BasicCSVParser::parse_loop(ParseLoopData& data)
    {
        // Optimizations
        auto* HEDLEY_RESTRICT parse_flags = data.parse_flags.data();
        auto* HEDLEY_RESTRICT ws_flags = data.ws_flags.data();

        // Parser state
        auto start_offset = data.start_offset;
        size_t current_row_start = 0;

        for (size_t i = 0; i < data.in.size(); ) {
            using internals::ParseFlags;
            switch (parse_flags[data.in[i] + 128]) {
            case ParseFlags::NOT_SPECIAL:
                data.raw_data->fields.push_back(
                    this->parse_field(data.in, parse_flags,
                        current_row_start,
                        i)
                );
                data.raw_data->fields.back().start += start_offset;
                break;

                HEDLEY_FALL_THROUGH;

            case ParseFlags::QUOTE:
                i++;
                quote_escape = true;
                data.raw_data->fields.push_back(
                    this->parse_quoted_field(data.in, parse_flags, current_row_start, i, quote_escape)
                );
                data.raw_data->fields.back().start += start_offset;

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
                if (i < data.in.size() && data.in[i] == '\n')
                    i++;

                data.records->push_back(std::move(current_row));

                if (data.is_stitching) {
                    return i;
                }

                current_row = RawCSVRow(data.raw_data);
                current_row.data_start = i;
                current_row.field_bounds_index = data.raw_data->fields.size();
                current_row_start = i;

                break;
            }
        }
    }

    std::string RawCSVRow::get_field(size_t index)
    {
        RawCSVField raw_field = this->data->fields[
            this->field_bounds_index + index
        ];

        auto csv_field = this->data->data.substr(
            this->data_start + raw_field.start,
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

    std::string RawCSVRow::get_raw_data()
    {
        return this->data->data.substr(
            this->data_start,
            this->get_raw_data_length()
        );
    }

    size_t RawCSVRow::get_raw_data_length()
    {
        size_t length = 0;
        for (auto& field : this->data->fields) {
            length += field.length;
        }

        return length;
    }
    std::vector<RawCSVField> RawCSVRow::get_raw_fields()
    {
        std::vector<RawCSVField> ret = {};
        for (size_t i = this->field_bounds_index; i < this->data->fields.size(); i++) {
            ret.push_back(this->data->fields[i]);
        }

        return ret;
    }
}