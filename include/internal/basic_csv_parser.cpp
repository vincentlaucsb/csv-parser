#include "basic_csv_parser.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE size_t get_file_size(csv::string_view filename) {
            std::ifstream infile(std::string(filename), std::ios::binary);
            const auto start = infile.tellg();
            infile.seekg(0, std::ios::end);
            const auto end = infile.tellg();

            return end - start;
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename) {
            return get_csv_head(filename, get_file_size(filename));
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename, size_t file_size) {
            const size_t bytes = 500000;

            std::error_code error;
            size_t length = std::min((size_t)file_size, bytes);
            auto mmap = mio::make_mmap_source(std::string(filename), 0, length, error);

            if (error) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            return std::string(mmap.begin(), mmap.end());
        }

#ifdef _MSC_VER
#pragma region IBasicCVParser
#endif
        CSV_INLINE IBasicCSVParser::IBasicCSVParser(
            const CSVFormat& format,
            const ColNamesPtr& col_names
        ) : _col_names(col_names) {
            if (format.no_quote) {
                _parse_flags = internals::make_parse_flags(format.get_delim());
            }
            else {
                _parse_flags = internals::make_parse_flags(format.get_delim(), format.quote_char);
            }

            _ws_flags = internals::make_ws_flags(
                format.trim_chars.data(), format.trim_chars.size()
            );
        }

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

        CSV_INLINE void IBasicCSVParser::parse_field() noexcept {
            using internals::ParseFlags;
            auto& in = this->data_ptr->data;

            // Trim off leading whitespace
            while (data_pos < in.size() && ws_flag(in[data_pos]))
                data_pos++;

            if (field_start == UNINITIALIZED_FIELD)
                field_start = (int)(data_pos - current_row_start());

            // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
            // sequences, use the loop below to avoid having to go through the outer
            // switch statement as much as possible
            while (data_pos < in.size() && compound_parse_flag(in[data_pos]) == ParseFlags::NOT_SPECIAL)
                data_pos++;

            field_length = data_pos - (field_start + current_row_start());

            // Trim off trailing whitespace, this->field_length constraint matters
            // when field is entirely whitespace
            for (size_t j = data_pos - 1; ws_flag(in[j]) && this->field_length > 0; j--)
                this->field_length--;
        }

        CSV_INLINE void IBasicCSVParser::push_field()
        {
            // Update
            if (field_has_double_quote) {
                fields->emplace_back(
                    field_start == UNINITIALIZED_FIELD ? 0 : (unsigned int)field_start,
                    field_length,
                    true
                );
                field_has_double_quote = false;

            }
            else {
                fields->emplace_back(
                    field_start == UNINITIALIZED_FIELD ? 0 : (unsigned int)field_start,
                    field_length
                );
            }

            current_row.row_length++;

            // Reset field state
            field_start = UNINITIALIZED_FIELD;
            field_length = 0;
        }

        /** @return The number of characters parsed that belong to complete rows */
        CSV_INLINE size_t IBasicCSVParser::parse()
        {
            using internals::ParseFlags;

            this->quote_escape = false;
            this->data_pos = 0;
            this->current_row_start() = 0;
            this->trim_utf8_bom();

            auto& in = this->data_ptr->data;
            while (this->data_pos < in.size()) {
                switch (compound_parse_flag(in[this->data_pos])) {
                case ParseFlags::DELIMITER:
                    this->push_field();
                    this->data_pos++;
                    break;

                case ParseFlags::NEWLINE:
                    this->data_pos++;

                    // Catches CRLF (or LFLF)
                    if (this->data_pos < in.size() && parse_flag(in[this->data_pos]) == ParseFlags::NEWLINE)
                        this->data_pos++;

                    // End of record -> Write record
                    this->push_field();
                    this->push_row();

                    // Reset
                    this->current_row = CSVRow(data_ptr, this->data_pos, fields->size());
                    break;

                case ParseFlags::NOT_SPECIAL:
                    this->parse_field();
                    break;

                case ParseFlags::QUOTE_ESCAPE_QUOTE:
                    if (data_pos + 1 == in.size()) return this->current_row_start();
                    else if (data_pos + 1 < in.size()) {
                        auto next_ch = parse_flag(in[data_pos + 1]);
                        if (next_ch >= ParseFlags::DELIMITER) {
                            quote_escape = false;
                            data_pos++;
                            break;
                        }
                        else if (next_ch == ParseFlags::QUOTE) {
                            // Case: Escaped quote
                            data_pos += 2;
                            this->field_length += 2;
                            this->field_has_double_quote = true;
                            break;
                        }
                    }
                    
                    // Case: Unescaped single quote => not strictly valid but we'll keep it
                    this->field_length++;
                    data_pos++;

                    break;

                default: // Quote (currently not quote escaped)
                    if (this->field_length == 0) {
                        quote_escape = true;
                        data_pos++;
                        if (field_start == UNINITIALIZED_FIELD && data_pos < in.size() && !ws_flag(in[data_pos]))
                            field_start = (int)(data_pos - current_row_start());
                        break;
                    }

                    // Case: Unescaped quote
                    this->field_length++;
                    data_pos++;

                    break;
                }
            }

            return this->current_row_start();
        }

        CSV_INLINE void IBasicCSVParser::push_row() {
            current_row.row_length = fields->size() - current_row.fields_start;
            this->_records->push_back(std::move(current_row));
        }

        CSV_INLINE void IBasicCSVParser::reset_data_ptr() {
            this->data_ptr = std::make_shared<RawCSVData>();
            this->data_ptr->parse_flags = this->_parse_flags;
            this->data_ptr->col_names = this->_col_names;
            this->fields = &(this->data_ptr->fields);
        }

        CSV_INLINE void IBasicCSVParser::trim_utf8_bom() {
            auto& data = this->data_ptr->data;

            if (!this->unicode_bom_scan && data.size() >= 3) {
                if (data[0] == '\xEF' && data[1] == '\xBB' && data[2] == '\xBF') {
                    this->data_pos += 3; // Remove BOM from input string
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
        CSV_INLINE void MmapParser::next(size_t bytes = ITERATION_CHUNK_SIZE) {
            // Reset parser state
            this->field_start = UNINITIALIZED_FIELD;
            this->field_length = 0;
            this->reset_data_ptr();

            // Create memory map
            size_t length = std::min(this->source_size - this->mmap_pos, bytes);
            std::error_code error;
            this->data_ptr->_data = std::make_shared<mio::basic_mmap_source<char>>(mio::make_mmap_source(this->_filename, this->mmap_pos, length, error));
            this->mmap_pos += length;
            if (error) throw error;

            auto mmap_ptr = (mio::basic_mmap_source<char>*)(this->data_ptr->_data.get());

            // Create string view
            this->data_ptr->data = csv::string_view(mmap_ptr->data(), mmap_ptr->length());

            // Parse
            this->current_row = CSVRow(this->data_ptr);
            size_t remainder = this->parse();            

            if (this->mmap_pos == this->source_size || no_chunk()) {
                this->_eof = true;
                this->end_feed();
            }

            this->mmap_pos -= (length - remainder);
        }
#ifdef _MSC_VER
#pragma endregion
#endif
    }
}
