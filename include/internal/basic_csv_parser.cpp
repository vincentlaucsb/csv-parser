#include "basic_csv_parser.hpp"

#include <system_error>

namespace csv {
    namespace internals {
#if defined(__EMSCRIPTEN__)
        // Opens the file and delegates to the template overload to avoid duplicating the read/resize logic.
        CSV_INLINE std::string get_csv_head_stream(csv::string_view filename) {
            std::ifstream infile(std::string(filename), std::ios::binary);
            if (!infile.is_open()) {
                throw_cannot_open_file(filename);
            }
            return get_csv_head_stream(infile);
        }
#endif

#if !defined(__EMSCRIPTEN__)
        CSV_INLINE std::pair<std::string, size_t> get_csv_head_mmap(csv::string_view filename) {
            const size_t bytes = 500000;
            std::error_code error;
            auto mmap = mio::make_mmap_source(std::string(filename), 0, mio::map_entire_file, error);
            if (error) {
                throw_cannot_open_file(filename);
            }
            const size_t file_size = mmap.size();
            const size_t length = std::min(file_size, bytes);
            return { std::string(mmap.begin(), mmap.begin() + length), file_size };
        }
#endif

        CSV_INLINE std::string get_csv_head(csv::string_view filename) {
#if defined(__EMSCRIPTEN__)
            return get_csv_head_stream(filename);
#else
            return get_csv_head_mmap(filename).first;
#endif
        }

        CSV_INLINE size_t get_bom_skip_or_throw(csv::string_view data, bool& utf8_bom) {
            utf8_bom = false;

            if (data.size() >= 4) {
                const unsigned char b0 = static_cast<unsigned char>(data[0]);
                const unsigned char b1 = static_cast<unsigned char>(data[1]);
                const unsigned char b2 = static_cast<unsigned char>(data[2]);
                const unsigned char b3 = static_cast<unsigned char>(data[3]);

                if ((b0 == 0xFF && b1 == 0xFE && b2 == 0x00 && b3 == 0x00)
                    || (b0 == 0x00 && b1 == 0x00 && b2 == 0xFE && b3 == 0xFF)) {
                    throw_unsupported_encoding("UTF-32");
                }
            }

            if (data.size() >= 3) {
                const unsigned char b0 = static_cast<unsigned char>(data[0]);
                const unsigned char b1 = static_cast<unsigned char>(data[1]);
                const unsigned char b2 = static_cast<unsigned char>(data[2]);

                if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {
                    utf8_bom = true;
                    return 3;
                }
            }

            if (data.size() >= 2) {
                const unsigned char b0 = static_cast<unsigned char>(data[0]);
                const unsigned char b1 = static_cast<unsigned char>(data[1]);

                if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
                    throw_unsupported_encoding("UTF-16");
                }
            }

            return 0;
        }


#ifdef _MSC_VER
#pragma region IBasicCSVParser
#endif
        CSV_INLINE IBasicCSVParser::IBasicCSVParser(
            const CSVFormat& source_format,
            const ColNamesPtr& col_names
        ) : col_names_(col_names) {
            // Only initialize the fields that are stable before format resolution.
            // parse_flags_ and simd_sentinels_ are always set by resolve_format_from_head,
            // so there is no point computing them here with a placeholder delimiter.
            ws_flags_ = internals::make_ws_flags(
                source_format.trim_chars.data(), source_format.trim_chars.size()
            );
            has_ws_trimming_ = !source_format.trim_chars.empty();
        }

        CSV_INLINE void IBasicCSVParser::resolve_format_from_head(const CSVFormat& source_format) {
            auto head = this->get_csv_head();

            ResolvedFormat resolved;
            resolved.format = source_format;

            const bool infer_delimiter = source_format.guess_delim();
            const bool infer_header = !source_format.header_explicitly_set_
                && (infer_delimiter || !source_format.col_names_explicitly_set_);
            const bool infer_n_cols = (source_format.get_header() < 0 && source_format.get_col_names().empty());

            if (infer_delimiter || infer_header || infer_n_cols) {
                auto guess_result = guess_format(head, source_format.get_possible_delims());
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
                this->push_row(this->data_ptr_ ? this->data_ptr_->data.size() : this->current_row_start());
        }

        CSV_INLINE void IBasicCSVParser::resolve_pending_quote_at_start(csv::string_view in) {
            using internals::ParseFlags;

            if (!this->pending_quote_ || this->data_pos_ >= in.size()) {
                return;
            }

            const ParseFlags next_ch = this->parse_flag(in[this->data_pos_]);
            this->pending_quote_ = false;

            if (next_ch >= ParseFlags::DELIMITER) {
                this->quote_escape_ = false;
                return;
            }

            if (next_ch == ParseFlags::QUOTE) {
                // Previous chunk ended on a quote and this chunk starts with a
                // quote, so the pair is an escaped quote inside the field.
                this->quote_escape_ = true;
                this->field_has_double_quote_ = true;
                this->field_length_++;
                this->data_pos_++;
                return;
            }

            // Non-strict CSV: a quote inside a quoted field followed by ordinary
            // content is kept as literal content, matching the existing DFA path.
            this->quote_escape_ = true;
            this->field_length_++;
        }

        CSV_INLINE void IBasicCSVParser::resolve_pending_linefeed_at_start(csv::string_view in) {
            using internals::ParseFlags;

            if (!this->pending_linefeed_ || this->data_pos_ >= in.size()) {
                return;
            }

            this->pending_linefeed_ = false;
            if (this->parse_flag(in[this->data_pos_]) == ParseFlags::NEWLINE) {
                this->data_pos_++;
                this->current_row_start() = this->data_pos_;
            }
        }

        CSV_FORCE_INLINE void IBasicCSVParser::parse_field() noexcept {
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

        CSV_FORCE_INLINE void IBasicCSVParser::push_field()
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

        CSV_INLINE ParserChunkResult IBasicCSVParser::parse_chunk(
            csv::string_view chunk,
            std::shared_ptr<void> owner
        ) {
            return this->parse_prepared_chunk(
                chunk,
                std::move(owner),
                ParserChunkOptions(this->initial_state_)
            );
        }

        CSV_INLINE ParserChunkResult IBasicCSVParser::parse_chunk(
            csv::string_view chunk,
            std::shared_ptr<void> owner,
            const ParserChunkOptions& options
        ) {
            return this->parse_prepared_chunk(chunk, std::move(owner), options);
        }

        CSV_INLINE ParserChunkResult IBasicCSVParser::parse_chunk(
            csv::string_view chunk,
            std::shared_ptr<void> owner,
            CSVRowSink& sink
        ) {
            this->records_ = nullptr;
            this->row_sink_ = &sink;
            return this->parse_prepared_chunk(
                chunk,
                std::move(owner),
                ParserChunkOptions(this->initial_state_)
            );
        }

        CSV_INLINE ParserChunkResult IBasicCSVParser::parse_chunk(
            csv::string_view chunk,
            std::shared_ptr<void> owner,
            CSVRowSink& sink,
            const ParserChunkOptions& options
        ) {
            this->records_ = nullptr;
            this->row_sink_ = &sink;
            return this->parse_prepared_chunk(chunk, std::move(owner), options);
        }

        CSV_INLINE ParserChunkResult IBasicCSVParser::parse_prepared_chunk(
            csv::string_view chunk,
            std::shared_ptr<void> owner,
            const ParserChunkOptions& options
        ) {
            this->field_start_ = UNINITIALIZED_FIELD;
            this->field_length_ = 0;
            this->field_has_double_quote_ = false;
            this->reset_data_ptr();
            this->data_ptr_->_data = std::move(owner);
            this->data_ptr_->data = chunk;
            this->initial_state_ = options.initial_state;
            this->scan_bom_for_current_chunk_ = options.scan_bom;
            this->current_row_ = CSVRow(this->data_ptr_);

            const size_t complete_prefix_length = this->parse();
            return ParserChunkResult(options.initial_state, this->ending_state_, complete_prefix_length);
        }

        /** Parse the current chunk and return the number of bytes belonging to complete rows. */
        CSV_INLINE size_t IBasicCSVParser::parse()
        {
            using internals::ParseFlags;

            const ParserDFAState start_state = this->initial_state_;
            this->quote_escape_ = start_state.quote_escape;
            this->pending_quote_ = start_state.pending_quote;
            this->pending_linefeed_ = start_state.pending_linefeed;
            this->data_pos_ = 0;
            this->current_row_start() = 0;
            if (this->scan_bom_for_current_chunk_) {
                this->strip_unicode_bom();
            }

            auto& in = this->data_ptr_->data;
            this->resolve_pending_linefeed_at_start(in);
            this->resolve_pending_quote_at_start(in);
            while (this->data_pos_ < in.size()) {
                switch (compound_parse_flag(in[this->data_pos_])) {
                case ParseFlags::DELIMITER:
                    this->push_field();
                    this->data_pos_++;
                    break;

                case ParseFlags::CARRIAGE_RETURN:
                {
                    const size_t raw_end = this->data_pos_;
                    // Handles CRLF (we do not advance by 2 here, the NEWLINE case will handle it)
                    if (this->data_pos_ + 1 == in.size()) {
                        this->pending_linefeed_ = true;
                    }
                    else if (parse_flag(in[this->data_pos_ + 1]) == ParseFlags::NEWLINE) {
                        this->data_pos_++;
                    }
                    this->data_pos_++;
                    this->finish_row(raw_end);
                    break;
                }

                case ParseFlags::NEWLINE:
                {
                    const size_t raw_end = this->data_pos_;
                    this->data_pos_++;
                    this->finish_row(raw_end);
                    break;
                }

                case ParseFlags::NOT_SPECIAL:
                    this->parse_field();
                    break;

                case ParseFlags::QUOTE_ESCAPE_QUOTE:
                    if (data_pos_ + 1 == in.size()) {
                        this->pending_quote_ = true;
                        return this->finish_parse(this->current_row_start());
                    }
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

            return this->finish_parse(this->current_row_start());
        }

        CSV_INLINE void IBasicCSVParser::finish_row(size_t raw_end) {
            // End of record. Preserve intentional empty fields such as trailing
            // delimiters and quoted empty strings, but leave a truly blank line
            // as an empty row.
            if (this->field_length_ > 0
                || this->field_start_ != UNINITIALIZED_FIELD
                || !this->current_row_.empty()) {
                this->push_field();
            }

            this->push_row(raw_end);
            this->current_row_ = CSVRow(data_ptr_, this->data_pos_, fields_->size());
        }

        CSV_INLINE void IBasicCSVParser::push_row(size_t raw_end) {
            size_t row_len = fields_->size() - current_row_.fields_start;
            // Set row_length before pushing (immutable once created)
            current_row_.row_length = row_len;
            current_row_.data_end = raw_end;
            this->emit_row(std::move(current_row_));
        }

        CSV_INLINE void IBasicCSVParser::reset_data_ptr() {
            this->data_ptr_ = std::make_shared<RawCSVData>();
            this->data_ptr_->parse_flags = this->parse_flags_;
            this->data_ptr_->ws_flags = this->ws_flags_;
            this->data_ptr_->has_ws_trimming = this->has_ws_trimming_;
            this->data_ptr_->col_names = this->col_names_;
            this->fields_ = &(this->data_ptr_->fields);
        }

        CSV_INLINE void IBasicCSVParser::strip_unicode_bom() {
            auto& data = this->data_ptr_->data;

            if (!this->unicode_bom_scan_) {
                this->data_pos_ += get_bom_skip_or_throw(data, this->utf8_bom_);
                this->unicode_bom_scan_ = true;
            }
        }
#ifdef _MSC_VER
#pragma endregion
#endif

    }
}
