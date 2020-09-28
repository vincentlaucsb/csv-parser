#include "raw_csv_data.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE void BasicCSVParser::parse(csv::string_view in) {
            this->set_data_ptr(std::make_shared<RawCSVData>());
            this->data_ptr->col_names = this->col_names;

            // Check for previous fragments
            if (this->current_row.data && this->current_row.size() > 0 || this->field_length > 0) {
                // Make a separate data buffer for the fragment row
                auto temp_str = this->current_row.data->data.substr(this->current_row.data_start);

                this->current_row.data = this->data_ptr;
                this->current_row.data_start = 0;
                this->current_row.row_length = 0;
                this->current_row.field_bounds_index = 0;

                this->field_start = -1;
                this->field_length = 0;

                auto& fragment_data = this->current_row.data;
                fragment_data->data.reserve(temp_str.size() + in.size());
                fragment_data->data = temp_str;
                fragment_data->data += in;

                in = csv::string_view(fragment_data->data);
            }
            else {
                this->data_ptr->data.assign(in.data(), in.size());
                this->current_row = CSVRow(this->data_ptr);
            }

            this->parse_loop(in);
        }

        CSV_INLINE void BasicCSVParser::push_field()
        {
            this->fields->push_back({
                this->field_start > 0 ? (unsigned int)this->field_start : 0,
                this->field_length
            });

            this->current_row.row_length++;

            if (this->field_has_double_quote) {
                this->current_row.data->has_double_quotes.insert(this->data_ptr->fields.size() - 1);
                this->field_has_double_quote = false;
            }

            // Reset field state
            this->field_start = -1;
            this->field_length = 0;
        }

        CSV_INLINE void BasicCSVParser::parse_loop(csv::string_view in)
        {
            using internals::ParseFlags;

            // Parser state
            size_t current_row_start = 0;
            bool quote_escape = false;

            size_t in_size = in.size();
            for (size_t i = 0; i < in_size; ) {
                switch (compound_parse_flag(in[i], quote_escape)) {
                case CompoundParseFlags::DELIMITER:
                    this->push_field();
                    i++;
                    break;

                case CompoundParseFlags::NEWLINE:
                    i++;

                    // Catches CRLF (or LFLF)
                    if (i < in.size() && parse_flag(in[i]) == ParseFlags::NEWLINE) i++;

                    // End of record -> Write record
                    this->push_field();
                    this->push_row();

                    // Reset
                    this->current_row = CSVRow(this->data_ptr);
                    this->current_row.data_start = i;
                    this->current_row.field_bounds_index = this->data_ptr->fields.size();
                    current_row_start = i;
                    break;

                case CompoundParseFlags::NOT_SPECIAL:
                    this->parse_field<false>(in, i, current_row_start);
                    break;

                case CompoundParseFlags::QUOTE_ESCAPE_QUOTE:                
                    if (parse_flag(in[i]) == ParseFlags::QUOTE) {
                        if (i + 1 < in.size() && parse_flag(in[i + 1]) >= ParseFlags::DELIMITER
                            || i + 1 == in.size()) {
                            quote_escape = false;
                            i++;
                            continue;
                        }

                        // Case: Escaped quote
                        this->field_length++;
                        i++;

                        if (i < in.size() && parse_flag(in[i]) == ParseFlags::QUOTE) {
                            i++;
                            this->field_length++;
                            this->field_has_double_quote = true;
                        }

                        continue;
                    }
                    
                case CompoundParseFlags::QUOTE_ESCAPE_NOT_SPECIAL:
                    this->parse_field<true>(in, i, current_row_start);
                    break;

                default: // Quote (not quote escaped)
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