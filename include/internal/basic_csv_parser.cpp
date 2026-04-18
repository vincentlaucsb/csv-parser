#include "basic_csv_parser.hpp"

#include <system_error>

namespace csv {
    namespace internals {
        CSV_INLINE size_t get_file_size(csv::string_view filename) {
            std::ifstream infile(std::string(filename), std::ios::binary);
            if (!infile.is_open()) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            const auto start = infile.tellg();
            infile.seekg(0, std::ios::end);
            const auto end = infile.tellg();

            if (start < 0 || end < 0) {
                throw std::runtime_error("Cannot determine file size for " + std::string(filename));
            }

            return static_cast<size_t>(end - start);
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename) {
            return get_csv_head(filename, get_file_size(filename));
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename, size_t file_size) {
            const size_t bytes = 500000;

#if defined(__EMSCRIPTEN__)
            std::ifstream infile(std::string(filename), std::ios::binary);
            if (!infile.is_open()) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            const size_t length = std::min((size_t)file_size, bytes);
            std::string head(length, '\0');
            infile.read(&head[0], (std::streamsize)length);
            head.resize((size_t)infile.gcount());
            return head;
#else

            std::error_code error;
            size_t length = std::min((size_t)file_size, bytes);
            auto mmap = mio::make_mmap_source(std::string(filename), 0, length, error);

            if (error) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            return std::string(mmap.begin(), mmap.end());
#endif
        }

#ifdef _MSC_VER
#pragma region IBasicCVParser
#endif
        CSV_INLINE IBasicCSVParser::IBasicCSVParser(
            const CSVFormat& format,
            const ColNamesPtr& col_names
        ) : col_names_(col_names) {
            if (format.no_quote) {
                parse_flags_ = internals::make_parse_flags(format.get_delim());
            }
            else {
                parse_flags_ = internals::make_parse_flags(format.get_delim(), format.quote_char);
            }

            // When no_quote, quote bytes are NOT_SPECIAL — use delimiter as safe dummy
            // so SIMD does not stop early on quote bytes and cause an infinite loop.
            const char eff_quote = format.no_quote ? format.get_delim() : format.quote_char;
            simd_sentinels_ = SentinelVecs(format.get_delim(), eff_quote);
            ws_flags_ = internals::make_ws_flags(
                format.trim_chars.data(), format.trim_chars.size()
            );
            has_ws_trimming_ = !format.trim_chars.empty();
        }

        CSV_INLINE void IBasicCSVParser::end_feed() {
            using internals::ParseFlags;

            bool empty_last_field = this->data_ptr_
                && this->data_ptr_->_data
                && !this->data_ptr_->data.empty()
                && (parse_flag(this->data_ptr_->data.back()) == ParseFlags::DELIMITER
                    || parse_flag(this->data_ptr_->data.back()) == ParseFlags::QUOTE);

            // Push field
            if (this->field_length_ > 0 || empty_last_field) {
                this->push_field();
            }

            // Push row
            if (this->current_row_.size() > 0)
                this->push_row();
        }

        CSV_INLINE void IBasicCSVParser::parse_field() noexcept {
            using internals::ParseFlags;
            auto& in = this->data_ptr_->data;

            if (field_start_ == UNINITIALIZED_FIELD)
                field_start_ = (int)(data_pos_ - current_row_start());

            // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
            // sequences, use SIMD to skip long runs of them quickly.
            // find_next_non_special processes complete SIMD lanes and returns pos
            // unchanged for any tail shorter than one lane width.
#if !defined(CSV_NO_SIMD)
            data_pos_ = find_next_non_special(in, data_pos_, this->simd_sentinels_);
#endif

            // Scalar tail: handles remaining bytes after SIMD falls through, and
            // handles any byte that SIMD stopped at conservatively (e.g. a delimiter
            // inside a quoted field, which compound_parse_flag treats as NOT_SPECIAL).
            while (data_pos_ < in.size() && compound_parse_flag(in[data_pos_]) == ParseFlags::NOT_SPECIAL)
                data_pos_++;

            field_length_ = data_pos_ - (field_start_ + current_row_start());

            // Whitespace trimming is deferred to get_field_impl() so callers that never
            // read field values (e.g. row counting) pay no trimming cost.
        }

        CSV_INLINE void IBasicCSVParser::push_field()
        {
            // Update
            fields_->emplace_back(
                field_start_ == UNINITIALIZED_FIELD ? 0 : (unsigned int)field_start_,
                field_length_,
                field_has_double_quote_
            );

            current_row_.row_length++;

            // Reset field state
            field_has_double_quote_ = false;
            field_start_ = UNINITIALIZED_FIELD;
            field_length_ = 0;
        }

        /** @return The number of characters parsed that belong to complete rows */
        CSV_INLINE size_t IBasicCSVParser::parse()
        {
            using internals::ParseFlags;

            this->quote_escape_ = false;
            this->data_pos_ = 0;
            this->current_row_start() = 0;
            this->trim_utf8_bom();

            auto& in = this->data_ptr_->data;
            while (this->data_pos_ < in.size()) {
                switch (compound_parse_flag(in[this->data_pos_])) {
                case ParseFlags::DELIMITER:
                    this->push_field();
                    this->data_pos_++;
                    break;

                case ParseFlags::NEWLINE:
                    this->data_pos_++;

                    // Catches CRLF (or LFLF, CRCRLF, or any other non-sensical combination of newlines)
                    while (this->data_pos_ < in.size() && parse_flag(in[this->data_pos_]) == ParseFlags::NEWLINE)
                        this->data_pos_++;

                    // End of record -> Write non-empty record
                    if (this->field_length_ > 0 || !this->current_row_.empty()) {
                        this->push_field();
                        this->push_row();
                    }

                    // Reset
                    this->current_row_ = CSVRow(data_ptr_, this->data_pos_, fields_->size());
                    break;

                case ParseFlags::NOT_SPECIAL:
                    this->parse_field();
                    break;

                case ParseFlags::QUOTE_ESCAPE_QUOTE:
                    if (data_pos_ + 1 == in.size()) return this->current_row_start();
                    else if (data_pos_ + 1 < in.size()) {
                        auto next_ch = parse_flag(in[data_pos_ + 1]);
                        if (next_ch >= ParseFlags::DELIMITER) {
                            quote_escape_ = false;
                            data_pos_++;
                            break;
                        }
                        else if (next_ch == ParseFlags::QUOTE) {
                            // Case: Escaped quote
                            data_pos_ += 2;
                            this->field_length_ += 2;
                            this->field_has_double_quote_ = true;
                            break;
                        }
                    }
                    
                    // Case: Unescaped single quote => not strictly valid but we'll keep it
                    this->field_length_++;
                    data_pos_++;

                    break;

                default: // Quote (currently not quote escaped)
                    if (this->field_length_ == 0) {
                        quote_escape_ = true;
                        data_pos_++;
                        if (field_start_ == UNINITIALIZED_FIELD && data_pos_ < in.size() && !ws_flag(in[data_pos_]))
                            field_start_ = (int)(data_pos_ - current_row_start());
                        break;
                    }

                    // Case: Unescaped quote
                    this->field_length_++;
                    data_pos_++;

                    break;
                }
            }

            return this->current_row_start();
        }

        CSV_INLINE void IBasicCSVParser::push_row() {
            size_t row_len = fields_->size() - current_row_.fields_start;
            // Set row_length before pushing (immutable once created)
            current_row_.row_length = row_len;
            this->records_->push_back(std::move(current_row_));
        }

        CSV_INLINE void IBasicCSVParser::reset_data_ptr() {
            this->data_ptr_ = std::make_shared<RawCSVData>();
            this->data_ptr_->parse_flags = this->parse_flags_;
            this->data_ptr_->ws_flags = this->ws_flags_;
            this->data_ptr_->has_ws_trimming = this->has_ws_trimming_;
            this->data_ptr_->col_names = this->col_names_;
            this->fields_ = &(this->data_ptr_->fields);
        }

        CSV_INLINE void IBasicCSVParser::trim_utf8_bom() {
            auto& data = this->data_ptr_->data;

            if (!this->unicode_bom_scan_ && data.size() >= 3) {
                if (data[0] == '\xEF' && data[1] == '\xBB' && data[2] == '\xBF') {
                    this->data_pos_ += 3; // Remove BOM from input string
                    this->utf8_bom_ = true;
                }

                this->unicode_bom_scan_ = true;
            }
        }
#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef _MSC_VER
#pragma region Specializations
#endif
#if !defined(__EMSCRIPTEN__)
        CSV_INLINE void MmapParser::next(size_t bytes = ITERATION_CHUNK_SIZE) {
            // CRITICAL SECTION: Chunk Transition Logic
            // This function reads 10MB chunks and must correctly handle fields that span
            // chunk boundaries. The 'remainder' calculation below ensures partial fields
            // are preserved for the next chunk.
            //
            // Bug #280: Field corruption occurred here when chunk transitions incorrectly
            // split multi-byte characters or field boundaries.
            
            // Reset parser state
            this->field_start_ = UNINITIALIZED_FIELD;
            this->field_length_ = 0;
            this->reset_data_ptr();

            // Create memory map
            const size_t offset = this->mmap_pos;
            const size_t remaining = (offset < this->source_size_)
                ? (this->source_size_ - offset)
                : 0;
            const size_t length = std::min(remaining, bytes);
            if (length == 0) {
                // No more data to read; mark EOF and end feed
                // (Prevent exception on empty mmap as reported by #267)
                this->eof_ = true;
                this->end_feed();
                return;
            }

            std::error_code error;
            auto mmap = mio::make_mmap_source(this->_filename, offset, length, error);
            if (error) {
                std::string msg = "Memory mapping failed during CSV parsing: file='" + this->_filename
                    + "' offset=" + std::to_string(offset)
                    + " length=" + std::to_string(length);
                throw std::system_error(error, msg);
            }
            this->data_ptr_->_data = std::make_shared<mio::basic_mmap_source<char>>(std::move(mmap));
            this->mmap_pos += length;

            auto mmap_ptr = (mio::basic_mmap_source<char>*)(this->data_ptr_->_data.get());

            // Create string view
            this->data_ptr_->data = csv::string_view(mmap_ptr->data(), mmap_ptr->length());

            // Parse
            this->current_row_ = CSVRow(this->data_ptr_);
            size_t remainder = this->parse();            

            if (this->mmap_pos == this->source_size_ || no_chunk()) {
                this->eof_ = true;
                this->end_feed();
            }

            this->mmap_pos -= (length - remainder);
        }
#endif
#ifdef _MSC_VER
#pragma endregion
#endif
    }
}
