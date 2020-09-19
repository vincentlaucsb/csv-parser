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
        ParseLoopData main_loop_data;
        main_loop_data.raw_data = std::make_shared<RawCSVData>();
        main_loop_data.parse_flags = _parse_flags;
        main_loop_data.ws_flags = _ws_flags;

        // Check for previous fragments
        if (this->current_row.row_length > 0) {
            // Make a separate data buffer for the fragment row
            auto temp_str = this->current_row.data->data.substr(
                this->current_row.data_start
            );
            // auto temp_fields = this->current_row.get_raw_fields();
            auto temp_row_length = this->current_row.row_length;

            this->current_row.data = std::make_shared<RawCSVData>();
            this->current_row.data_start = 0;
            this->current_row.row_length = 0;
            this->current_row.field_bounds_index = 0;

            this->field_start = 0;
            this->field_length = 0;
            this->quote_escape = false;

            auto& fragment_data = this->current_row.data;
            fragment_data->data = temp_str + in.data();
            // fragment_data->fields = temp_fields;

            in = csv::string_view(fragment_data->data);
        }
        else {
            this->current_row = RawCSVRow(main_loop_data.raw_data);
        }
        
        main_loop_data.in = in;
        main_loop_data.raw_data->data.assign(in.data(), in.size());
        main_loop_data.records = &records;

        this->parse_loop(main_loop_data);

        // Return True if we are done parsing and there are no
        // incomplete rows or fields
        return this->current_row.row_length == 0;
    }

    void BasicCSVParser::parse_quoted_field(
        csv::string_view in, internals::ParseFlags parse_flags[],
        size_t& i) {
        using internals::ParseFlags;
        bool has_double_quote = false;
        this->field_start = i - this->current_row_start;
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

        this->field_length += length;

        // TODO: Double check this
        this->field_has_double_quote = has_double_quote;
    }

    void BasicCSVParser::push_field()
    {
        // Push field
        this->current_row.row_length++;
        this->current_row.data->fields.push_back({
            this->field_start,
            this->field_length,
            this->field_has_double_quote
        });

        // Reset field state
        this->field_start = 0;
        this->field_length = 0;
        this->field_has_double_quote = false;
    }

    void BasicCSVParser::parse_field(csv::string_view in, internals::ParseFlags parse_flags[], size_t& i) {
        using internals::ParseFlags;

        this->field_start = i - this->current_row_start;
        this->field_has_double_quote = false;

        size_t length = 0;

        // We'll allow unescaped quotes...
        for (; i < in.size() && parse_flags[in[i] + 128] <= ParseFlags::QUOTE; i++) {
            length++;
        }

        this->field_length += length;
    }

    size_t BasicCSVParser::parse_loop(ParseLoopData& data)
    {
        // Optimizations
        auto* HEDLEY_RESTRICT parse_flags = data.parse_flags.data();
        auto* HEDLEY_RESTRICT ws_flags = data.ws_flags.data();

        // Parser state
        this->current_row_start = 0;
        auto start_offset = data.start_offset;

        for (size_t i = 0; i < data.in.size(); ) {
            using internals::ParseFlags;
            switch (parse_flags[data.in[i] + 128]) {
            case ParseFlags::NOT_SPECIAL:
                if (!this->quote_escape) {
                    this->parse_field(data.in, parse_flags, i);
                    this->field_start += start_offset;
                    break;
                }

                HEDLEY_FALL_THROUGH;

            case ParseFlags::QUOTE:
                if (!this->quote_escape) {
                    i++;
                    this->quote_escape = true;
                }

                this->parse_quoted_field(data.in, parse_flags, i);
                this->field_start += start_offset;

                break;

            case ParseFlags::DELIMITER:
                this->push_field();
                i++;
                break;

            default: // Newline
                // End of record -> Write record
                this->push_field();
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

        auto csv_field = this->data->data.substr(this->data_start + raw_field.start);

        if (raw_field.has_doubled_quote) {
            std::string ret = "";
            bool prev_ch_quote = false;

            for (size_t i = 0;
                (i < csv_field.size())
                && (ret.size() < this->row_length);
                i++) {
                // TODO: Use parse flags
                if (csv_field[i] == '"') {
                    if (prev_ch_quote) {
                        prev_ch_quote = false;
                        continue;
                    }
                    else {
                        prev_ch_quote = true;
                    }
                }

                ret += csv_field[i];
            }

            return ret;
        }

        csv_field = csv_field.substr(0, raw_field.length);
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