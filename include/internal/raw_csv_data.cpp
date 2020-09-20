#include "raw_csv_data.hpp"

namespace csv {
    bool BasicCSVParser::parse(
        csv::string_view in,
        std::deque<CSVRow>& records
    ) {
        using internals::ParseFlags;

        size_t part_of_previous_fragment = 0;
        ParseLoopData main_loop_data;
        main_loop_data.raw_data = std::make_shared<RawCSVData>();
        main_loop_data.raw_data->col_names = this->col_names;

        // Check for previous fragments
        if (this->current_row.row_length > 0) {
            // Make a separate data buffer for the fragment row
            auto temp_str = this->current_row.data->data.substr(
                this->current_row.data_start
            );
            auto temp_row_length = this->current_row.row_length;

            this->current_row.data = std::make_shared<RawCSVData>();
            this->current_row.data_start = 0;
            this->current_row.row_length = 0;
            this->current_row.field_bounds_index = 0;

            this->field_start = 0;
            this->field_length = 0;
            this->quote_escape = false;

            auto& fragment_data = this->current_row.data;
            fragment_data->col_names = this->col_names;
            fragment_data->data = temp_str + in.data();
            // fragment_data->fields = temp_fields;

            in = csv::string_view(fragment_data->data);
        }
        else {
            this->current_row = CSVRow(main_loop_data.raw_data);
        }

        main_loop_data.in = in;
        main_loop_data.raw_data->data.assign(in.data(), in.size());
        main_loop_data.records = &records;

        this->parse_loop(main_loop_data);

        // Return True if we are done parsing and there are no
        // incomplete rows or fields
        return this->current_row.row_length == 0;
    }

    void BasicCSVParser::parse_quoted_field(csv::string_view in, size_t& i) {
        using internals::ParseFlags;
        this->field_start = i - this->current_row_start;

        auto* HEDLEY_RESTRICT parse_flags = this->parse_flags.data();

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
                        this->field_has_double_quote = true;
                        i++;
                    }

                    // Fallthrough
                }
            }

            this->field_length++;
        }        
    }

    void BasicCSVParser::push_field()
    {
        // Push field
        this->current_row.row_length++;
        this->current_row.data->fields.push_back({
            this->field_start,
            this->field_length,
        });

        if (this->field_has_double_quote) {
            this->current_row.data->has_double_quotes.insert(this->current_row.data->fields.size() - 1);
        }

        // Reset field state
        this->field_start = 0;
        this->field_length = 0;
        this->field_has_double_quote = false;
    }

    void BasicCSVParser::parse_field(csv::string_view in, size_t& i) {
        using internals::ParseFlags;
        auto* HEDLEY_RESTRICT parse_flags = this->parse_flags.data();

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
        auto* HEDLEY_RESTRICT parse_flags = this->parse_flags.data();
        auto* HEDLEY_RESTRICT ws_flags = this->ws_flags.data();

        // Parser state
        this->current_row_start = 0;
        auto start_offset = data.start_offset;

        for (size_t i = 0; i < data.in.size(); ) {
            using internals::ParseFlags;
            switch (parse_flags[data.in[i] + 128]) {
            case ParseFlags::NOT_SPECIAL:
                if (!this->quote_escape) {
                    this->parse_field(data.in, i);
                    this->field_start += start_offset;
                    break;
                }

                HEDLEY_FALL_THROUGH;

            case ParseFlags::QUOTE:
                if (!this->quote_escape) {
                    i++;
                    this->quote_escape = true;
                }

                this->parse_quoted_field(data.in, i);
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

                this->push_row(*(data.records));

                if (data.is_stitching) {
                    return i;
                }

                current_row = CSVRow(data.raw_data);
                current_row.data_start = i;
                current_row.field_bounds_index = data.raw_data->fields.size();
                current_row_start = i;

                break;
            }
        }
    }
}