#include "basic_csv_parser.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE void IBasicCSVParser::parse_field(csv::string_view in, size_t& i) noexcept {
            using internals::ParseFlags;

            // Trim off leading whitespace
            while (i < in.size() && ws_flag(in[i])) i++;

            if (field_start == UNINITIALIZED_FIELD)
                field_start = (int)(i - current_row_start());

            // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
            // sequences, use the loop below to avoid having to go through the outer
            // switch statement as much as possible
            while (i < in.size() && compound_parse_flag(in[i]) == ParseFlags::NOT_SPECIAL) i++;

            field_length = i - (field_start + current_row_start());

            // Trim off trailing whitespace, this->field_length constraint matters
            // when field is entirely whitespace
            for (size_t j = i - 1; ws_flag(in[j]) && this->field_length > 0; j--) this->field_length--;
        }

        CSV_INLINE void IBasicCSVParser::push_field()
        {
            // Update
            if (field_has_double_quote) {
                fields->emplace_back(
                    field_start == UNINITIALIZED_FIELD ? 0 : (unsigned int)this->field_start,
                    field_length,
                    true
                );
                field_has_double_quote = false;

            }
            else {
                fields->emplace_back(
                    field_start == UNINITIALIZED_FIELD ? 0 : (unsigned int)this->field_start,
                    field_length
                );
            }

            current_row.row_length++;

            // Reset field state
            field_start = UNINITIALIZED_FIELD;
            field_length = 0;
        }

        CSV_INLINE size_t IBasicCSVParser::parse_loop(csv::string_view in)
        {
            using internals::ParseFlags;

            this->quote_escape = false;
            this->current_row_start() = 0;

            for (size_t i = 0; i < in.size(); ) {
                switch (compound_parse_flag(in[i])) {
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
                    this->push_row();

                    // Reset
                    this->current_row = CSVRow(data_ptr, i, fields->size());
                    break;

                case ParseFlags::NOT_SPECIAL:
                    this->parse_field(in, i);
                    break;

                case ParseFlags::QUOTE_ESCAPE_QUOTE:
                    if (i + 1 == in.size()) return 0;
                    else if (i + 1 < in.size()) {
                        auto next_ch = parse_flag(in[i + 1]);
                        if (next_ch >= ParseFlags::DELIMITER) {
                            quote_escape = false;
                            i++;
                            break;
                        }
                        else if (next_ch == ParseFlags::QUOTE) {
                            // Case: Escaped quote
                            i += 2;
                            this->field_length += 2;
                            this->field_has_double_quote = true;
                            break;
                        }
                    }
                    
                    // Case: Unescaped single quote => not strictly valid but we'll keep it
                    this->field_length++;
                    i++;

                    break;

                default: // Quote (currently not quote escaped)
                    if (this->field_length == 0) {
                        quote_escape = true;
                        i++;
                        break;
                    }

                    // Case: Unescaped quote
                    this->field_length++;
                    i++;

                    break;
                }
            }

            return this->current_row_start();
        }
    }
}