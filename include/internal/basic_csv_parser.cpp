#include "basic_csv_parser.hpp"

#include <system_error>

// Because g++ wants to be a pedantic little brat about fallthroughs
#ifdef CXX_CSV_HAS_17
#define FALLTHROUGH_TO_NEXT_CASE [[fallthrough]];
#else
#define FALLTHROUGH_TO_NEXT_CASE goto next_newline_case;
#endif

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

        CSV_INLINE std::string get_csv_head_stream(csv::string_view filename) {
            const size_t bytes = 500000;
            std::ifstream infile(std::string(filename), std::ios::binary);
            if (!infile.is_open()) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            std::string head(bytes, '\0');
            infile.read(&head[0], (std::streamsize)bytes);
            head.resize((size_t)infile.gcount());
            return head;
        }

#if !defined(__EMSCRIPTEN__)
        CSV_INLINE std::string get_csv_head_mmap(csv::string_view filename) {
            const size_t bytes = 500000;
            const size_t file_size = get_file_size(filename);
            std::error_code error;
            const size_t length = std::min(file_size, bytes);
            auto mmap = mio::make_mmap_source(std::string(filename), 0, length, error);

            if (error) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            return std::string(mmap.begin(), mmap.end());
        }
#endif

        CSV_INLINE std::string get_csv_head(csv::string_view filename) {
#if defined(__EMSCRIPTEN__)
            return get_csv_head_stream(filename);
#else
            return get_csv_head_mmap(filename);
#endif
        }


#ifdef _MSC_VER
#pragma region IBasicCSVParser
#endif
        CSV_INLINE IBasicCSVParser::IBasicCSVParser(
            const CSVFormat& format,
            const ColNamesPtr& col_names
        ) : col_names_(col_names) {
            const char initial_delim = format.guess_delim()
                ? format.get_possible_delims().at(0)
                : format.get_delim();
            if (format.no_quote) {
                parse_flags_ = internals::make_parse_flags(initial_delim);
            }
            else {
                parse_flags_ = internals::make_parse_flags(initial_delim, format.quote_char);
            }

            // When no_quote, quote bytes are NOT_SPECIAL — use delimiter as safe dummy
            // so SIMD does not stop early on quote bytes and cause an infinite loop.
            const char eff_quote = format.no_quote ? initial_delim : format.quote_char;
            simd_sentinels_ = SentinelVecs(initial_delim, eff_quote);
            ws_flags_ = internals::make_ws_flags(
                format.trim_chars.data(), format.trim_chars.size()
            );
            has_ws_trimming_ = !format.trim_chars.empty();
        }

        CSV_INLINE void IBasicCSVParser::resolve_format_from_head(CSVFormat format) {
            auto head = this->get_csv_head();

            ResolvedFormat resolved;
            resolved.format = format;

            const bool infer_delimiter = format.guess_delim();
            const bool infer_header = !format.header_explicitly_set_
                && (infer_delimiter || !format.col_names_explicitly_set_);
            const bool infer_n_cols = (format.get_header() < 0 && format.get_col_names().empty());

            if (infer_delimiter || infer_header || infer_n_cols) {
                auto guess_result = guess_format(head, format.get_possible_delims());
                if (infer_delimiter) {
                    resolved.format.delimiter(guess_result.delim);
                }

                if (infer_header) {
                    // Inferred header should not clear user-provided column names.
                    resolved.format.header = guess_result.header_row;
                }

                resolved.n_cols = guess_result.n_cols;
            }

            if (resolved.format.no_quote) {
                parse_flags_ = internals::make_parse_flags(resolved.format.get_delim());
            }
            else {
                parse_flags_ = internals::make_parse_flags(resolved.format.get_delim(), resolved.format.quote_char);
            }
            const char resolved_eff_quote = resolved.format.no_quote
                ? resolved.format.get_delim()
                : resolved.format.quote_char;
            simd_sentinels_ = SentinelVecs(resolved.format.get_delim(), resolved_eff_quote);

            this->format = resolved;
        }
#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef _MSC_VER
#pragma region IBasicCVParser: Core Parse Loop
#endif
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

        /** Parse the current chunk and return the number of bytes belonging to complete rows. */
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

                case ParseFlags::CARRIAGE_RETURN:
                    // Handles CRLF (we do not advance by 2 here, the NEWLINE case will handle it)
                    if (this->data_pos_ + 1 < in.size() && parse_flag(in[this->data_pos_ + 1]) == ParseFlags::NEWLINE) {
                        this->data_pos_++;
                    }

                    FALLTHROUGH_TO_NEXT_CASE

                next_newline_case:
                case ParseFlags::NEWLINE:
                    this->data_pos_++;

                    // End of record. Preserve intentional empty fields such as
                    // trailing delimiters and quoted empty strings, but leave a
                    // truly blank line as an empty row.
                    if (this->field_length_ > 0
                        || this->field_start_ != UNINITIALIZED_FIELD
                        || !this->current_row_.empty()) {
                        this->push_field();
                    }
                    this->push_row();

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
        CSV_INLINE void MmapParser::finalize_loaded_chunk(size_t length, bool eof_on_no_chunk) {
            // Parse the currently loaded chunk and advance/re-align mmap_pos so
            // the next read resumes at the start of the incomplete trailing row.
            this->current_row_ = CSVRow(this->data_ptr_);
            size_t remainder = this->parse();

            if (this->mmap_pos == this->source_size_ || (eof_on_no_chunk && no_chunk())) {
                this->eof_ = true;
                this->end_feed();
            }

            this->mmap_pos -= (length - remainder);
        }

        CSV_INLINE void MmapParser::next(size_t bytes = CSV_CHUNK_SIZE_DEFAULT) {
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

            // Reuse the pre-read head buffer (if any) as the first chunk.
            // This avoids re-reading the same bytes that were already consumed
            // for delimiter/header guessing.
            if (!head_.empty()) {
                this->data_ptr_->_data = std::make_shared<std::string>(std::move(head_));
                auto* head_ptr = static_cast<std::string*>(this->data_ptr_->_data.get());
                const size_t length = head_ptr->size();
                this->mmap_pos += length;

                this->data_ptr_->data = *head_ptr;
                this->finalize_loaded_chunk(length);
                return;
            }

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
            this->finalize_loaded_chunk(length, true);
        }
#endif
#ifdef _MSC_VER
#pragma endregion
#endif
    }
}
