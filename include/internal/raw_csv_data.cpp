#include "raw_csv_data.hpp"
//#include <iostream>

namespace csv {
    bool BasicCSVParser::parse(csv::string_view in, std::deque<CSVRow>& records) {
        using internals::ParseFlags;

        this->set_data_ptr(std::make_shared<RawCSVData>());
        this->data_ptr->col_names = this->col_names;
        if (this->suggested_capacity > 0) {
            this->fields->reserve(this->suggested_capacity);
        }

        this->records = &records;

        // Check for previous fragments
        if (this->current_row.data && this->current_row.data->fields.size() - this->current_row.field_bounds_index > 0) {
            // Make a separate data buffer for the fragment row
            auto temp_str = this->current_row.data->data.substr(
                this->current_row.data_start
            );
            auto temp_row_length = this->current_row.row_length;

            this->current_row.data = this->data_ptr;
            this->current_row.data_start = 0;
            this->current_row.row_length = 0;
            this->current_row.field_bounds_index = 0;

            this->field_start = 0;
            this->field_length = 0;

            auto& fragment_data = this->current_row.data;
            fragment_data->data.reserve(temp_str.size() + in.size());
            fragment_data->data = temp_str;
            fragment_data->data += in.data();
            
            in = csv::string_view(fragment_data->data);
        }
        else {
            this->data_ptr->data.assign(in.data(), in.size());
            this->current_row = CSVRow(this->data_ptr);
        }

        this->parse_loop(in, 0);
        this->suggested_capacity = std::max(this->suggested_capacity, this->data_ptr->fields.size());

        // Return True if we are done parsing and there are no
        // incomplete rows or fields
        return this->current_row.row_length == 0;
    }

    void BasicCSVParser::push_field()
    {
        // Push field
        this->fields->push_back({
            this->field_start > 0 ? (unsigned int)this->field_start : 0,
            this->field_length
        });

        if (this->field_has_double_quote) {
            this->current_row.data->has_double_quotes.insert(this->data_ptr->fields.size() - 1);
            this->field_has_double_quote = false;
        }

        // Reset field state
        this->field_start = -1;
        this->field_length = 0;
    }

    CONSTEXPR void BasicCSVParser::parse_field(csv::string_view in, size_t& i, const size_t& current_row_start, bool quote_escape) {
        using internals::ParseFlags;

        // Trim off leading whitespace
        while (i < in.size() && ws_flag(in[i])) i++;

        if (this->field_start < 0) {
            this->field_start = i - current_row_start;
        }

        // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
        // sequences, use the loop below to avoid having to go through the outer
        // switch statement as much as possible
        if (quote_escape) {
            while (i < in.size() && parse_flag(in[i]) != ParseFlags::QUOTE) i++;
        }
        else {
            while (i < in.size() && parse_flag(in[i]) == ParseFlags::NOT_SPECIAL) i++;
        }

        this->field_length = i - (this->field_start + current_row_start);

        // Trim off trailing whitespace
        while (i < in.size() && ws_flag(in[i])) this->field_length--;
    }

    void BasicCSVParser::parse_loop(csv::string_view in, size_t start_offset)
    {
        using internals::ParseFlags;

        // Parser state
        size_t current_row_start = 0;
        bool quote_escape = false;

        size_t in_size = in.size();
        for (size_t i = 0; i < in_size; ) {
            if (quote_escape) {
                if (parse_flag(in[i]) == ParseFlags::QUOTE) {
                    if (i + 1 < in.size() && parse_flag(in[i + 1]) >= ParseFlags::DELIMITER) {
                        quote_escape = false;
                        i++;
                        continue;
                    }

                    // Case: Escaped quote
                    this->field_length++;
                    i++;

                    if (parse_flag(in[i]) == ParseFlags::QUOTE) {
                        i++;
                        this->field_length++;
                        this->field_has_double_quote = true;
                    }

                    continue;
                }
                
                this->parse_field(in, i, current_row_start, quote_escape);
            }
            else {
                switch (parse_flag(in[i])) {
                case ParseFlags::DELIMITER:
                    this->push_field();
                    i++;
                    break;

                case ParseFlags::NEWLINE:
                    i++;

                    // Catches CRLF (or LFLF)
                    if (i < in.size() && parse_flag(in[i]) == ParseFlags::NEWLINE) i++;

                    // End of record -> Write record
                    this->push_field();
                    this->push_row(*records);
                    this->current_row = CSVRow(this->data_ptr);
                    this->current_row.data_start = i;
                    this->current_row.field_bounds_index = this->data_ptr->fields.size();
                    current_row_start = i;
                    break;

                case ParseFlags::NOT_SPECIAL:
                    this->parse_field(in, i, current_row_start, quote_escape);
                    break;
                default: // Quote
                    if (this->field_length == 0) {
                        quote_escape = true;
                        i++;
                        break;
                    }

                    // Unescaped quote
                    this->field_length++;
                    i++;

                    break;
                }
            }
        }
    }
}