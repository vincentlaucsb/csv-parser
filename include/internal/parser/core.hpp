/** @file
 *  @brief Focused CSV byte parser core.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "../basic_csv_parser_simd.hpp"
#include "../col_names.hpp"
#include "../common.hpp"
#include "../csv_exceptions.hpp"
#include "../csv_format.hpp"
#include "../csv_row.hpp"
#include "../row_deque.hpp"

namespace csv {
    namespace internals {
        constexpr const int UNINITIALIZED_FIELD = -1;

        /** Helper constexpr function to initialize an array with all the elements set to value. */
        template<typename OutArray, typename T = typename OutArray::type>
        CSV_CONST CONSTEXPR_17 OutArray arrayToDefault(T&& value)
        {
            OutArray a {};
            for (auto& e : a)
                 e = value;
            return a;
        }

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        CSV_CONST CONSTEXPR_17 ParseFlagMap make_parse_flags(char delimiter) {
            auto ret = arrayToDefault<ParseFlagMap>(ParseFlags::NOT_SPECIAL);
            ret[delimiter + CHAR_OFFSET] = ParseFlags::DELIMITER;
            ret['\r' + CHAR_OFFSET] = ParseFlags::CARRIAGE_RETURN;
            ret['\n' + CHAR_OFFSET] = ParseFlags::NEWLINE;
            return ret;
        }

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        CSV_CONST CONSTEXPR_17 ParseFlagMap make_parse_flags(char delimiter, char quote_char) {
            std::array<ParseFlags, 256> ret = make_parse_flags(delimiter);
            ret[quote_char + CHAR_OFFSET] = ParseFlags::QUOTE;
            return ret;
        }

        inline char infer_char_for_flag(
            const ParseFlagMap& parse_flags,
            ParseFlags target,
            char fallback
        ) noexcept {
            for (size_t i = 0; i < parse_flags.size(); ++i) {
                if (parse_flags[i] == target) {
                    return static_cast<char>(static_cast<int>(i) - CHAR_OFFSET);
                }
            }

            return fallback;
        }

        inline char infer_delimiter(const ParseFlagMap& parse_flags) noexcept {
            return infer_char_for_flag(parse_flags, ParseFlags::DELIMITER, ',');
        }

        // fallback is returned when no QUOTE flag exists in parse_flags (e.g. no_quote mode).
        // Pass the delimiter so SIMD stops there instead of on a byte that is NOT_SPECIAL.
        inline char infer_quote_char(const ParseFlagMap& parse_flags, char fallback = '"') noexcept {
            return infer_char_for_flag(parse_flags, ParseFlags::QUOTE, fallback);
        }

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character c and, v[i + 128] is true if
         *  c is a whitespace character
         */
        CSV_CONST CONSTEXPR_17 WhitespaceMap make_ws_flags(const char* ws_chars, size_t n_chars) {
            auto ret = arrayToDefault<WhitespaceMap>(false);
            for (size_t j = 0; j < n_chars; j++) {
                ret[ws_chars[j] + CHAR_OFFSET] = true;
            }
            return ret;
        }

        inline WhitespaceMap make_ws_flags(const std::vector<char>& flags) {
            return make_ws_flags(flags.data(), flags.size());
        }

        /** Return the number of leading BOM bytes to skip, or throw for unsupported Unicode encodings. */
        CSV_INLINE size_t get_bom_skip_or_throw(csv::string_view data, bool& utf8_bom);

        /** Explicit DFA state at a parse boundary.
         *
         *  This is intentionally about parser control flow, not field metadata.
         *  `pending_quote` represents the ambiguous case where a chunk ended at
         *  a quote while already inside a quoted field; the next byte determines
         *  whether that quote closes the field, escapes a quote, or is kept as
         *  non-strict literal content.
         */
        struct ParserDFAState {
            ParserDFAState() noexcept = default;
            ParserDFAState(
                bool quote_escape,
                bool pending_quote = false,
                bool pending_linefeed = false
            ) noexcept
                : quote_escape(quote_escape),
                  pending_quote(pending_quote),
                  pending_linefeed(pending_linefeed) {}

            bool quote_escape = false;
            bool pending_quote = false;
            bool pending_linefeed = false;
        };

        inline bool parser_dfa_state_equal(ParserDFAState lhs, ParserDFAState rhs) noexcept {
            return lhs.quote_escape == rhs.quote_escape
                && lhs.pending_quote == rhs.pending_quote
                && lhs.pending_linefeed == rhs.pending_linefeed;
        }

        struct ParserChunkOptions {
            ParserChunkOptions() noexcept = default;
            explicit ParserChunkOptions(
                ParserDFAState initial_state,
                bool scan_bom = true,
                size_t source_start = 0
            ) noexcept
                : initial_state(initial_state),
                  scan_bom(scan_bom),
                  source_start(source_start) {}

            ParserDFAState initial_state;
            bool scan_bom = true;
            size_t source_start = 0;
        };

        struct ParserChunkResult {
            ParserChunkResult() noexcept = default;
            ParserChunkResult(
                ParserDFAState initial_state,
                ParserDFAState ending_state,
                size_t complete_prefix_length
            ) noexcept
                : initial_state(initial_state),
                  ending_state(ending_state),
                  complete_prefix_length(complete_prefix_length) {}

            ParserDFAState initial_state;
            ParserDFAState ending_state;
            size_t complete_prefix_length = 0;
        };

        struct CSVParseWindowResult {
            size_t complete_prefix_length = 0;
        };
    }

    /** Standard type for storing collection of rows. */
    using RowCollection = internals::ThreadSafeDeque<CSVRow>;

    namespace internals {
        /** Default parse policy hook.
         *
         *  The permissive policy intentionally does nothing. Calls are direct
         *  template calls that optimize away in the default parser, leaving a
         *  zero-cost extension point for future validation policies.
         */
        struct PermissiveParsePolicy {
            void begin_chunk(const RawCSVDataPtr&) noexcept {}
            void begin_row(const CSVRow&) noexcept {}
            void push_field(const RawCSVField&) noexcept {}
            void end_row(const CSVRow&) noexcept {}
        };

        /** Default field policy for the CSVRow path.
         *
         *  Owns RawCSVData/RawCSVFieldList setup and appends RawCSVField metadata
         *  exactly as the historical parser core did.
         */
        template<bool EagerClassify = false>
        struct CSVRowFieldPolicy {
            void begin_chunk(
                RawCSVDataPtr& data_ptr,
                RawCSVFieldList*& fields,
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags,
                bool has_ws_trimming,
                const ColNamesPtr& col_names
            ) const {
                data_ptr = std::make_shared<RawCSVData>();
                data_ptr->parse_flags = parse_flags;
                data_ptr->ws_flags = ws_flags;
                data_ptr->has_ws_trimming = has_ws_trimming;
                data_ptr->col_names = col_names;
                fields = &(data_ptr->fields);
            }

            const RawCSVField& push_field(
                RawCSVData& data,
                RawCSVFieldList& fields,
                size_t row_start,
                int field_start,
                size_t field_length,
                bool field_has_double_quote
            ) const {
                const size_t raw_start = field_start == UNINITIALIZED_FIELD
                    ? 0
                    : static_cast<size_t>(field_start);
                size_t stored_start = raw_start;
                size_t stored_length = field_length;
                bool is_realized = false;

                if (field_has_double_quote) {
                    stored_start = this->append_realized_quoted_field(
                        data,
                        row_start + raw_start,
                        field_length,
                        stored_length
                    );
                    is_realized = true;
                }

                fields.emplace_back(
                    stored_start,
                    stored_length,
                    is_realized
                );
                this->append_scalar(
                    data,
                    fields[fields.size() - 1],
                    row_start,
                    std::integral_constant<bool, EagerClassify>()
                );
                return fields[fields.size() - 1];
            }

        private:
            void append_scalar(
                RawCSVData&,
                const RawCSVField&,
                size_t,
                std::false_type
            ) const {}

            void append_scalar(
                RawCSVData& data,
                const RawCSVField& field,
                size_t row_start,
                std::true_type
            ) const {
                csv::string_view field_str;
                if (field.has_realized_storage()) {
                    field_str = data.quote_arena.view(field.start, field.length);
                }
                else {
                    field_str = csv::string_view(data.data).substr(row_start + field.start, field.length);
                }

                if (data.has_ws_trimming) {
                    field_str = internals::get_trimmed(field_str, data.ws_flags);
                }

                data.field_scalars.emplace_back(internals::classify_field_scalar(field_str));
            }

            CSVChunkIndex append_realized_quoted_field(
                RawCSVData& data,
                size_t field_start,
                size_t field_length,
                size_t& realized_length
            ) const {
                using internals::ParseFlags;

                const csv::string_view field_str = csv::string_view(data.data).substr(field_start, field_length);
                // Allocate the original length as an upper bound, then compact doubled
                // quotes in one pass. Wasting a byte per escaped quote pair is cheaper
                // than scanning quote-heavy fields twice in the parser hot path.
                auto allocation = data.quote_arena.allocate_contiguous(field_str.size());
                char* out = allocation.data;
                for (size_t i = 0; i < field_str.size(); ++i) {
                    if (data.parse_flags[field_str[i] + CHAR_OFFSET] == ParseFlags::QUOTE
                        && i + 1 < field_str.size()
                        && data.parse_flags[field_str[i + 1] + CHAR_OFFSET] == ParseFlags::QUOTE) {
                        *(out++) = field_str[i++];
                        continue;
                    }

                    *(out++) = field_str[i];
                }

                realized_length = static_cast<size_t>(out - allocation.data);
                return allocation.offset;
            }
        };

        /** Default row policy for the CSVRow path. */
        struct CSVRowRowPolicy {
            CSVRow make_initial_row(const RawCSVDataPtr& data_ptr) const {
                return CSVRow(data_ptr);
            }

            CSVRow make_next_row(
                const RawCSVDataPtr& data_ptr,
                size_t data_pos,
                size_t fields_size
            ) const {
                return CSVRow(data_ptr, data_pos, fields_size);
            }

            void finalize_row(
                CSVRow& row,
                const RawCSVFieldList& fields,
                size_t raw_end
            ) const {
                row.row_length = fields.size() - row.fields_start;
                row.data_end = raw_end;
            }
        };

        inline void csv_push_row(RowCollection& sink, CSVRow&& row) {
            sink.push_back(std::move(row));
        }

        inline void csv_push_row(std::vector<CSVRow>& sink, CSVRow&& row) {
            sink.push_back(std::move(row));
        }

        inline void csv_append_rows(RowCollection& sink, std::vector<CSVRow>&& rows) {
            sink.append_rows(std::move(rows));
        }

        inline void csv_append_rows(std::vector<CSVRow>& sink, std::vector<CSVRow>&& rows) {
            if (rows.empty()) {
                return;
            }

            sink.reserve(sink.size() + rows.size());
            for (auto& row : rows) {
                sink.push_back(std::move(row));
            }
        }

        template<
            typename RowSink = RowCollection,
            typename ParsePolicy = PermissiveParsePolicy,
            typename FieldPolicy = CSVRowFieldPolicy<false>,
            typename RowPolicy = CSVRowRowPolicy>
        class CSVParserCore {
        public:
            CSVParserCore() = default;

            CSVParserCore(
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

            CSVParserCore(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags
            ) : parse_flags_(parse_flags),
                ws_flags_(ws_flags) {
                const char d = internals::infer_delimiter(parse_flags);
                simd_sentinels_ = SentinelVecs(d, internals::infer_quote_char(parse_flags, d));
                has_ws_trimming_ = std::any_of(ws_flags.begin(), ws_flags.end(), [](bool b) { return b; });
            }

            CSVParserCore(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags,
                const ColNamesPtr& col_names
            ) : CSVParserCore(parse_flags, ws_flags) {
                this->col_names_ = col_names;
            }

            ~CSVParserCore() {}

            /** Indicate the last block of data has been parsed. */
            void end_feed() {
                using internals::ParseFlags;

                bool empty_last_field = this->data_ptr_
                    && this->data_ptr_->_data
                    && !this->data_ptr_->data.empty()
                    && (parse_flag(this->data_ptr_->data.back()) == ParseFlags::DELIMITER
                        || parse_flag(this->data_ptr_->data.back()) == ParseFlags::QUOTE);

                if (this->field_length_ > 0 || empty_last_field) {
                    this->push_field();
                }

                if (this->current_row_.size() > 0) {
                    this->push_row(this->data_ptr_ ? this->data_ptr_->data.size() : this->current_row_start());
                }
            }

            CONSTEXPR_17 ParseFlags parse_flag(const char ch) const noexcept {
                return parse_flags_.data()[ch + CHAR_OFFSET];
            }

            CONSTEXPR_17 ParseFlags compound_parse_flag(const char ch) const noexcept {
                return quote_escape_flag(parse_flag(ch), this->quote_escape_);
            }

            /** Whether or not this CSV has a UTF-8 byte order mark. */
            CONSTEXPR bool utf8_bom() const { return this->utf8_bom_; }

            void set_output(RowSink& output) noexcept {
                this->output_ = &output;
            }

            RowSink& output() noexcept {
                return *this->output_;
            }

            /** Seed the DFA state for the next parse call. */
            void reset_with_initial_state(ParserDFAState state) noexcept {
                this->initial_state_ = state;
            }

            /** Convenience overload for callers that only care about quote state. */
            void reset_with_initial_state(bool starts_in_quoted, bool in_escape = false) noexcept {
                this->reset_with_initial_state(ParserDFAState{ starts_in_quoted, in_escape });
            }

            /** Return the DFA state observed when the most recent parse call stopped. */
            ParserDFAState ending_state() const noexcept {
                return this->ending_state_;
            }

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner
            ) {
                return this->parse_prepared_chunk(
                    chunk,
                    std::move(owner),
                    ParserChunkOptions(this->initial_state_)
                );
            }

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                const ParserChunkOptions& options
            ) {
                return this->parse_prepared_chunk(chunk, std::move(owner), options);
            }

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                RowSink& output
            ) {
                return this->parse_chunk(
                    chunk,
                    std::move(owner),
                    output,
                    ParserChunkOptions(this->initial_state_)
                );
            }

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                RowSink& output,
                size_t source_start
            ) {
                return this->parse_chunk(
                    chunk,
                    std::move(owner),
                    output,
                    ParserChunkOptions(this->initial_state_, true, source_start)
                );
            }

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                RowSink& output,
                const ParserChunkOptions& options
            ) {
                this->output_ = &output;
                return this->parse_prepared_chunk(
                    chunk,
                    std::move(owner),
                    options
                );
            }

        protected:
            void set_col_names(const ColNamesPtr& col_names) {
                this->col_names_ = col_names;
            }

            void set_whitespace_flags(const WhitespaceMap& ws_flags) {
                this->ws_flags_ = ws_flags;
                this->has_ws_trimming_ = std::any_of(ws_flags.begin(), ws_flags.end(), [](bool b) { return b; });
            }

            void set_parse_flags(const ParseFlagMap& parse_flags) {
                this->parse_flags_ = parse_flags;
                const char d = internals::infer_delimiter(parse_flags);
                this->simd_sentinels_ = SentinelVecs(d, internals::infer_quote_char(parse_flags, d));
            }

            void set_parse_flags(const ParseFlagMap& parse_flags, char delimiter, char quote_char) {
                this->parse_flags_ = parse_flags;
                this->simd_sentinels_ = SentinelVecs(delimiter, quote_char);
            }

            /** @name Current Parser State */
            ///@{
            CSVRow current_row_;
            RawCSVDataPtr data_ptr_ = nullptr;
            ColNamesPtr col_names_ = nullptr;
            RawCSVFieldList* fields_ = nullptr;
            int field_start_ = UNINITIALIZED_FIELD;
            size_t field_length_ = 0;

            /** Precomputed SIMD broadcast vectors for find_next_non_special. */
            SentinelVecs simd_sentinels_;

            /** An array where the (i + 128)th slot gives the ParseFlags for ASCII character i. */
            ParseFlagMap parse_flags_;
            ///@}

            /** Parse the current chunk of data and return the completed-row prefix length. */
            size_t parse() {
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

                // Resolve any pending state from the previous chunk in speculative parsing
                this->resolve_pending_linefeed_at_start(in);
                this->resolve_pending_quote_at_start(in);

                while (this->data_pos_ < in.size()) {
                    const size_t raw_end = this->data_pos_;
                    switch (compound_parse_flag(in[this->data_pos_])) {
                    case ParseFlags::DELIMITER:
                        this->push_field();
                        this->data_pos_++;
                        break;

                    case ParseFlags::CARRIAGE_RETURN:
                        if (this->data_pos_ + 1 == in.size()) {
                            this->pending_linefeed_ = true;
                            this->data_pos_++;
                            this->finish_row(raw_end);
                            break;
                        }
                        else if (parse_flag(in[this->data_pos_ + 1]) == ParseFlags::NEWLINE) {
                            this->data_pos_++;
                        }

                        CSV_FALLTHROUGH;

                    case ParseFlags::NEWLINE:
                        this->data_pos_++;
                        this->finish_row(raw_end);
                        break;

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
                                data_pos_ += 2;
                                this->field_length_ += 2;
                                this->field_has_double_quote_ = true;
                                break;
                            }
                        }

                        this->field_length_++;
                        data_pos_++;
                        break;

                    default:
                        if (this->field_length_ == 0) {
                            quote_escape_ = true;
                            data_pos_++;
                            if (field_start_ == UNINITIALIZED_FIELD && data_pos_ < in.size() && !ws_flag(in[data_pos_]))
                                field_start_ = (int)(data_pos_ - current_row_start());
                            break;
                        }

                        this->field_length_++;
                        data_pos_++;
                        break;
                    }
                }

                return this->finish_parse(this->current_row_start());
            }

            /** Create a new RawCSVDataPtr for a new chunk of data. */
            void reset_data_ptr() {
                this->field_policy_.begin_chunk(
                    this->data_ptr_,
                    this->fields_,
                    this->parse_flags_,
                    this->ws_flags_,
                    this->has_ws_trimming_,
                    this->col_names_
                );
            }

            const WhitespaceMap& whitespace_flags() const noexcept {
                return this->ws_flags_;
            }

            void emit_row(CSVRow&& row) {
                if (this->output_) {
                    csv_push_row(*this->output_, std::move(row));
                }
            }

        private:
            /** An array where the (i + 128)th slot determines whether ASCII character i should
             *  be trimmed.
             */
            WhitespaceMap ws_flags_;

            /** True when at least one whitespace trim character is configured.
             *  Used to skip trim loops entirely in the common no-trim case.
             */
            bool has_ws_trimming_ = false;
            bool quote_escape_ = false;
            bool pending_quote_ = false;
            bool pending_linefeed_ = false;
            bool field_has_double_quote_ = false;
            ParserDFAState initial_state_;
            ParserDFAState ending_state_;
            bool scan_bom_for_current_chunk_ = true;

            /** Where we are in the current data block. */
            size_t data_pos_ = 0;

            /** Whether or not an attempt to find Unicode BOM has been made. */
            bool unicode_bom_scan_ = false;
            bool utf8_bom_ = false;

            RowSink* output_ = nullptr;
            ParsePolicy policy_;
            FieldPolicy field_policy_;
            RowPolicy row_policy_;

            CONSTEXPR_17 bool ws_flag(const char ch) const noexcept {
                return ws_flags_.data()[ch + CHAR_OFFSET];
            }

            size_t& current_row_start() {
                return this->current_row_.data_start;
            }

            void resolve_pending_quote_at_start(csv::string_view in) {
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
                    this->quote_escape_ = true;
                    this->field_has_double_quote_ = true;
                    this->field_length_++;
                    this->data_pos_++;
                    return;
                }

                this->quote_escape_ = true;
                this->field_length_++;
            }

            void resolve_pending_linefeed_at_start(csv::string_view in) {
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

            void parse_field() noexcept {
                using internals::ParseFlags;
                auto& in = this->data_ptr_->data;

                if (field_start_ == UNINITIALIZED_FIELD)
                    field_start_ = (int)(data_pos_ - current_row_start());

#if !defined(CSV_NO_SIMD)
                data_pos_ = find_next_non_special(in, data_pos_, this->simd_sentinels_);
#endif

                while (data_pos_ < in.size() && compound_parse_flag(in[data_pos_]) == ParseFlags::NOT_SPECIAL)
                    data_pos_++;

                field_length_ = data_pos_ - (field_start_ + current_row_start());
            }

            /** Finish parsing the current field. */
            void push_field() {
                const RawCSVField& field = this->field_policy_.push_field(
                    *this->data_ptr_,
                    *this->fields_,
                    this->current_row_start(),
                    this->field_start_,
                    this->field_length_,
                    this->field_has_double_quote_
                );

                this->policy_.push_field(field);
                current_row_.row_length++;

                field_has_double_quote_ = false;
                field_start_ = UNINITIALIZED_FIELD;
                field_length_ = 0;
            }

            /** Finish parsing the current record and reset row-local state. */
            void finish_row(size_t raw_end) {
                if (this->field_length_ > 0
                    || this->field_start_ != UNINITIALIZED_FIELD
                    || !this->current_row_.empty()) {
                    this->push_field();
                }

                this->push_row(raw_end);
                this->current_row_ = this->row_policy_.make_next_row(
                    this->data_ptr_,
                    this->data_pos_,
                    this->fields_->size()
                );
                this->policy_.begin_row(this->current_row_);
            }

            /** Finish parsing the current row. */
            void push_row(size_t raw_end) {
                this->row_policy_.finalize_row(this->current_row_, *this->fields_, raw_end);
                this->policy_.end_row(this->current_row_);
                this->emit_row(std::move(current_row_));
            }

            /** Handle possible Unicode byte order mark. */
            void strip_unicode_bom() {
                auto& data = this->data_ptr_->data;

                if (!this->unicode_bom_scan_) {
                    this->data_pos_ += get_bom_skip_or_throw(data, this->utf8_bom_);
                    this->unicode_bom_scan_ = true;
                }
            }

            ParserChunkResult parse_prepared_chunk(
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
                this->data_ptr_->source_start = options.source_start;
                this->data_ptr_->fields.reserve_for_source_size(chunk.size());
                this->data_ptr_->field_scalars.reserve_for_source_size(chunk.size());
                this->data_ptr_->quote_arena.reserve_for_source_size(chunk.size());
                this->initial_state_ = options.initial_state;
                this->scan_bom_for_current_chunk_ = options.scan_bom;
                this->current_row_ = this->row_policy_.make_initial_row(this->data_ptr_);
                this->policy_.begin_chunk(this->data_ptr_);
                this->policy_.begin_row(this->current_row_);

                const size_t complete_prefix_length = this->parse();
                return ParserChunkResult(options.initial_state, this->ending_state_, complete_prefix_length);
            }

            ParserDFAState current_dfa_state() const noexcept {
                return ParserDFAState{ this->quote_escape_, this->pending_quote_, this->pending_linefeed_ };
            }

            size_t finish_parse(size_t remainder) {
                this->ending_state_ = this->current_dfa_state();
                this->initial_state_ = this->ending_state_.pending_linefeed
                    ? this->ending_state_
                    : ParserDFAState{};
                this->scan_bom_for_current_chunk_ = true;
                return remainder;
            }
        };
    }
}
