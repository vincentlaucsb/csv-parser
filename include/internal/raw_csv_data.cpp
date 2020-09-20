#include "raw_csv_data.hpp"

namespace csv {
    bool BasicCSVParser::parse(
        csv::string_view in,
        std::deque<CSVRow>& records
    ) {
        using internals::ParseFlags;

        this->data_ptr = std::make_shared<RawCSVData>();
        this->data_ptr->col_names = this->col_names;
        this->records = &records;

        size_t part_of_previous_fragment = 0;
        
        // Check for previous fragments
        if (this->current_row.row_length > 0) {
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
            this->quote_escape = false;

            auto& fragment_data = this->current_row.data;
            fragment_data->col_names = this->col_names;
            fragment_data->data = temp_str + in.data();
            // fragment_data->fields = temp_fields;

            in = csv::string_view(fragment_data->data);
        }
        else {
            this->data_ptr->data.assign(in.data(), in.size());
            this->current_row = CSVRow(this->data_ptr);
        }

        this->parse_loop(in, 0);

        // Return True if we are done parsing and there are no
        // incomplete rows or fields
        return this->current_row.row_length == 0;
    }

    void BasicCSVParser::push_field()
    {
        // Push field
        this->current_row.row_length++;
        this->current_row.data->fields.push_back({
            (size_t)this->field_start,
            this->field_length
        });

        if (this->field_has_double_quote) {
            this->current_row.data->has_double_quotes.insert(this->current_row.data->fields.size() - 1);
        }

        // Reset field state
        this->quote_escape = false;
        this->field_start = -1;
        this->field_length = 0;
        this->field_has_double_quote = false;
    }

    void BasicCSVParser::parse_loop(csv::string_view in, size_t start_offset)
    {
        // Optimizations
        auto* HEDLEY_RESTRICT ws_flags = this->ws_flags.data();

        // Parser state
        this->current_row_start = 0;
        this->quote_escape = false;

        size_t in_size = in.size();
        for (size_t i = 0; i < in_size; ) {
            using internals::ParseFlags;
            switch(parse_flag(in[i])) {
            case ParseFlags::DELIMITER:
                if (!this->quote_escape) {
                    this->push_field();
                    i++;
                    break;
                }

                HEDLEY_FALL_THROUGH;

            case ParseFlags::NEWLINE:
                if (!this->quote_escape) {
                    ++i;

                    if (i < in.size() && parse_flag(in[i]) == ParseFlags::NEWLINE) // Catches CRLF (or LFLF)
                        ++i;

                    // End of record -> Write record
                    this->push_field();
                    this->push_row(*records);
                    this->current_row = CSVRow(this->data_ptr);
                    this->current_row.data_start = i;
                    this->current_row.field_bounds_index = this->data_ptr->fields.size();
                    this->current_row_start = i;
                    break;
                }

                // Treat as regular character
                this->field_length++;
                i++;
                break;

            case ParseFlags::NOT_SPECIAL:
                // Trim off leading whitespace
                while (i < in.size() && ws_flag(in[i])) {
                    i++;
                }

                if (this->field_start < 0) {
                    this->field_start = i - this->current_row_start;
                }

                // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
                // sequences, use the loop below to avoid having to go through the outer
                // switch statement as much as possible
                while (i < in.size() && parse_flag(in[i]) == ParseFlags::NOT_SPECIAL) {
                    i++;
                    this->field_length++;
                }

                // Trim off trailing whitespace
                while (i < in.size() && ws_flag(in[i])) {
                    i++;
                }

                break;
            default: // Quote
                if (!this->quote_escape) {
                    if (this->field_length == 0) {
                        this->quote_escape = true;
                        i++;
                    }
                }
                else if (i + 1 < in.size()) {
                    if (parse_flag(in[i + 1]) >= ParseFlags::DELIMITER) {
                        this->quote_escape = false;
                        i++;
                        break;
                    }

                    // Case: Escaped quote
                    i++;
                    this->field_length++;

                    // Note: Unescaped single quotes can be handled by the parser
                    if (parse_flag(in[i + 1]) == ParseFlags::QUOTE) {
                        i++;
                        this->field_length++;
                        this->field_has_double_quote = true;
                    }
                }
                else {
                    i++;
                }

                break;
            }
        }
    }
}