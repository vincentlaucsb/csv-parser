#include "raw_csv_data.hpp"

namespace csv {
    CSV_INLINE size_t BasicCSVParser::parse(csv::string_view in, RowCollection& records, bool last_block) {
        using internals::ParseFlags;

        this->set_data_ptr(std::make_shared<RawCSVData>());
        this->data_ptr->col_names = this->col_names;
        this->data_ptr->data.assign(in.data(), in.size());

        this->_records = &records;

        return this->parse_loop(in, last_block);
    }

    CSV_INLINE void BasicCSVParser::push_field(
        CSVRow& row,
        int& field_start,
        size_t& field_length,
        bool& has_double_quote)
    {
        // Push field
        this->fields->push_back({
            field_start > 0 ? (unsigned int)field_start : 0,
            field_length
        });
        row.row_length++;

        if (has_double_quote) {
            row.data->has_double_quotes.insert(this->data_ptr->fields.size() - 1);
            has_double_quote = false;
        }

        // Reset field state
        field_start = -1;
        field_length = 0;
    }

    CSV_INLINE size_t BasicCSVParser::parse_loop(csv::string_view in, bool last_block)
    {
        using internals::ParseFlags;

        // Parser state
        CSVRow row(this->data_ptr);
        size_t current_row_start = 0;
        int field_start = -1;
        size_t field_length = 0;
        bool has_double_quote = false;
        bool quote_escape = false;

        size_t in_size = in.size();
        for (size_t i = 0; i < in_size; ) {
            if (quote_escape) {
                if (parse_flag(in[i]) == ParseFlags::QUOTE) {
                    if (i + 1 < in.size() && parse_flag(in[i + 1]) >= ParseFlags::DELIMITER
                       || i + 1 == in.size()) {
                        quote_escape = false;
                        i++;
                        continue;
                    }

                    // Case: Escaped quote
                    field_length++;
                    i++;

                    if (i < in.size() && parse_flag(in[i]) == ParseFlags::QUOTE) {
                        i++;
                        field_length++;
                        has_double_quote = true;
                    }

                    continue;
                }
                
                this->parse_field<true>(in, i, field_start, field_length, current_row_start);
            }
            else {
                switch (parse_flag(in[i])) {
                case ParseFlags::DELIMITER:
                    this->push_field(row, field_start, field_length, has_double_quote);
                    i++;
                    break;

                case ParseFlags::NEWLINE:
                    i++;

                    // Catches CRLF (or LFLF)
                    if (i < in.size() && parse_flag(in[i]) == ParseFlags::NEWLINE) i++;

                    // End of record -> Write record
                    this->push_field(row, field_start, field_length, has_double_quote);
                    this->push_row(std::move(row), *this->_records);

                    // Reset
                    row = CSVRow(this->data_ptr);
                    row.data_start = i;
                    row.field_bounds_index = this->data_ptr->fields.size();
                    current_row_start = i;
                    break;

                case ParseFlags::NOT_SPECIAL:
                    this->parse_field<false>(in, i, field_start, field_length, current_row_start);
                    break;
                default: // Quote
                    if (field_length == 0) {
                        quote_escape = true;
                        i++;
                        break;
                    }

                    // Unescaped quote
                    field_length++;
                    i++;

                    break;
                }
            }
        }

        if (last_block) {
            // Indicates last field has no content (but is indeed a separate field)
            // e.g. 1,2,3,
            bool empty_last_field = parse_flag(in.back()) == ParseFlags::DELIMITER;

            // Push last field
            if (field_length > 0 || empty_last_field)
                this->push_field(row, field_start, field_length, has_double_quote);

            // Pust last row
            if (row.size() > 0)
                this->push_row(std::move(row), *_records);
        }
        else if (current_row_start < in_size) {
            return in.length() - current_row_start;
        }

        return 0;
    }
}