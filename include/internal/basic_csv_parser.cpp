#include "basic_csv_parser.hpp"

namespace csv {
    namespace internals {
#ifdef _MSC_VER
#pragma region IBasicCVParser
#endif
        CSV_INLINE void IBasicCSVParser::end_feed() {
            using internals::ParseFlags;

            bool empty_last_field = this->data_ptr
                && this->data_ptr->_data
                && !this->data_ptr->data.empty()
                && parse_flag(this->data_ptr->data.back()) == ParseFlags::DELIMITER;

            // Push field
            if (this->field_length > 0 || empty_last_field) {
                this->push_field();
            }

            // Push row
            if (this->current_row.size() > 0)
                this->push_row();
        }

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

        /** @return The number of lingering characters in the last
         *          unfinished row
         */
        CSV_INLINE size_t IBasicCSVParser::parse_loop()
        {
            using internals::ParseFlags;

            this->quote_escape = false;
            this->current_row_start() = 0;
            this->trim_utf8_bom();

            auto& in = this->data_ptr->data;
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

        CSV_INLINE void IBasicCSVParser::trim_utf8_bom() {
            /** Handle possible Unicode byte order mark */
            if (!this->unicode_bom_scan) {
                auto& data = this->data_ptr->data;

                if (data[0] == '\xEF' && data[1] == '\xBB' && data[2] == '\xBF') {
                    data.remove_prefix(3); // Remove BOM from input string
                    this->_utf8_bom = true;
                }

                this->unicode_bom_scan = true;
            }
        }
#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef _MSC_VER
#pragma region Specializations
#endif
        CSV_INLINE void BasicMmapParser::next() {
            // Reset parser state
            this->field_start = UNINITIALIZED_FIELD;
            this->field_length = 0;
            this->reset_data_ptr();

            // Create memory map
            size_t length = std::min(this->file_size - this->mmap_pos, csv::internals::ITERATION_CHUNK_SIZE);
            std::error_code error;
            this->data_ptr->_data = std::make_shared<mio::basic_mmap_source<char>>(mio::make_mmap_source(this->_filename, this->mmap_pos, length, error));
            this->mmap_pos += length;
            if (error) throw error;

            auto mmap_ptr = (mio::basic_mmap_source<char>*)(this->data_ptr->_data.get());

            // Create string view
            this->data_ptr->data = csv::string_view(mmap_ptr->data(), mmap_ptr->length());

            // Parse
            this->current_row = CSVRow(this->data_ptr);
            size_t remainder = this->parse_loop();

            // Re-align
            if (remainder > 0) {
                this->mmap_pos -= (length - remainder);
            }

            if (this->mmap_pos == this->file_size) {
                this->_eof = true;
                this->end_feed();
            }
        }
#ifdef _MSC_VER
#pragma endregion
#endif
    }
}