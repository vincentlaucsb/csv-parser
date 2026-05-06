/** @file
 *  @brief Contains the main CSV parsing algorithm and various utility functions
 */

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#include "../external/mio.hpp"
#endif
#include "basic_csv_parser_simd.hpp"
#include "col_names.hpp"
#include "common.hpp"
#include "csv_exceptions.hpp"
#include "csv_format.hpp"
#include "csv_row.hpp"
#include "csv_speculative_diagnostics.hpp"
#include "row_deque.hpp"

namespace csv {
    namespace internals {
        constexpr const int UNINITIALIZED_FIELD = -1;

        /** Helper constexpr function to initialize an array with all the elements set to value
         */
        template<typename OutArray, typename T = typename OutArray::type>
        CSV_CONST CONSTEXPR_17 OutArray arrayToDefault(T&& value)
        {
            OutArray a {};
            for (auto& e : a)
                 e = value;
            return a;
        }

        struct GuessScore {
            size_t header;
            size_t mode_row_length;
            double score;
        };

        CSV_INLINE GuessScore calculate_score(csv::string_view head, const CSVFormat& format);

        /** Guess the delimiter used by a delimiter-separated values file. */
        CSV_INLINE CSVGuessResult guess_format(
            csv::string_view head,
            const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' }
        );

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

        /** Read the first 500KB from a seekless stream source. */
        template<typename TStream,
            csv::enable_if_t<std::is_base_of<std::istream, TStream>::value, int>  = 0>
        std::string get_csv_head_stream(TStream& source) {
            const size_t limit = 500000;
            std::string buf(limit, '\0');
            source.read(&buf[0], (std::streamsize)limit);
            buf.resize(static_cast<size_t>(source.gcount()));
            return buf;
        }

#if defined(__EMSCRIPTEN__)
        /** Open a file-backed source and read the first 500KB through the stream path. */
        CSV_INLINE std::string get_csv_head_stream(csv::string_view filename);
#endif

#if !defined(__EMSCRIPTEN__)
        /** Read the first 500KB from a filename using mmap.
         *  Also returns the total file size so callers avoid a second mmap open.
         */
        CSV_INLINE std::pair<std::string, size_t> get_csv_head_mmap(csv::string_view filename);
#endif

        /** Compatibility shim selecting stream on Emscripten and mmap otherwise. */
        CSV_INLINE std::string get_csv_head(csv::string_view filename);

        struct ResolvedFormat {
            CSVFormat format;
            size_t n_cols = 0;
        };

        class CSVParserDriverBase;
    }

    /** @brief Guess the delimiter, header row, and mode column count of a CSV file
         *
         *  **Heuristic:** For each candidate delimiter, calculate a score based on
         *  the most common row length (mode). The delimiter with the highest score wins.
         *
         *  **Header Detection:**
         *  - If the first row has >= columns than the mode, it's treated as the header
         *  - Otherwise, the first row with the mode length is treated as the header
         *
         *  This approach handles:
         *  - Headers with trailing delimiters or optional columns (wider than data rows)
         *  - Comment lines before the actual header (first row shorter than mode)
         *  - Standard CSVs where first row is the header
         *
        *  @note Score = (row_length � count_of_rows_with_that_length)
        *  @note Also returns inferred mode-width column count (CSVGuessResult::n_cols)
         */
    inline CSVGuessResult guess_format(csv::string_view filename,
        const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' }) {
        auto head = internals::get_csv_head(filename);
        return internals::guess_format(head, delims);
    }

    /** Standard type for storing collection of rows */
    using RowCollection = internals::ThreadSafeDeque<CSVRow>;

    namespace internals {
        class CSVRowSink {
        public:
            virtual ~CSVRowSink() {}
            virtual void push_row(CSVRow&& row) = 0;
        };

        class VectorRowSink : public CSVRowSink {
        public:
            VectorRowSink() = default;
            explicit VectorRowSink(std::vector<CSVRow>& rows) : rows_(&rows) {}

            void reset(std::vector<CSVRow>& rows) noexcept {
                rows_ = &rows;
            }

            void push_row(CSVRow&& row) override {
                rows_->push_back(std::move(row));
            }

        private:
            std::vector<CSVRow>* rows_ = nullptr;
        };

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
            explicit ParserChunkOptions(ParserDFAState initial_state, bool scan_bom = true) noexcept
                : initial_state(initial_state), scan_bom(scan_bom) {}

            ParserDFAState initial_state;
            bool scan_bom = true;
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

        /** Non-virtual CSV byte parser core.
         *
         *  Source adapters feed caller-owned chunks into this class. It owns
         *  the DFA state, row/field construction, BOM handling, and row output
         *  routing, but knows nothing about files, streams, or mmap windows.
         */
        class CSVParserCore {
        public:
            CSVParserCore() = default;
            CSVParserCore(const CSVFormat&, const ColNamesPtr&);
            CSVParserCore(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags
            ) : parse_flags_(parse_flags), ws_flags_(ws_flags) {
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

            /** Indicate the last block of data has been parsed */
            void end_feed();

            CONSTEXPR_17 ParseFlags parse_flag(const char ch) const noexcept {
                return parse_flags_.data()[ch + CHAR_OFFSET];
            }

            CONSTEXPR_17 ParseFlags compound_parse_flag(const char ch) const noexcept {
                return quote_escape_flag(parse_flag(ch), this->quote_escape_);
            }

            /** Whether or not this CSV has a UTF-8 byte order mark */
            CONSTEXPR bool utf8_bom() const { return this->utf8_bom_; }

            void set_output(RowCollection& rows) {
                this->records_ = &rows;
                this->row_sink_ = nullptr;
            }

            void set_output(CSVRowSink& sink) noexcept {
                this->records_ = nullptr;
                this->row_sink_ = &sink;
            }

            void push_output_row(CSVRow&& row) {
                this->emit_row(std::move(row));
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
            );

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                const ParserChunkOptions& options
            );

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                CSVRowSink& sink
            );

            ParserChunkResult parse_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                CSVRowSink& sink,
                const ParserChunkOptions& options
            );

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

            /** Precomputed SIMD broadcast vectors for find_next_non_special */
            SentinelVecs simd_sentinels_;

            /** An array where the (i + 128)th slot gives the ParseFlags for ASCII character i */
            ParseFlagMap parse_flags_;
            ///@}

            /** Parse the current chunk of data and return the completed-row prefix length. */
            size_t parse();

            /** Create a new RawCSVDataPtr for a new chunk of data */
            void reset_data_ptr();

            const WhitespaceMap& whitespace_flags() const noexcept {
                return this->ws_flags_;
            }

            void emit_row(CSVRow&& row) {
                if (this->records_) {
                    this->records_->push_back(std::move(row));
                }
                else {
                    this->row_sink_->push_row(std::move(row));
                }
            }
        private:
            /** An array where the (i + 128)th slot determines whether ASCII character i should
             *  be trimmed
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

            /** Where we are in the current data block */
            size_t data_pos_ = 0;

            /** Whether or not an attempt to find Unicode BOM has been made */
            bool unicode_bom_scan_ = false;
            bool utf8_bom_ = false;

            /** Where serial parser rows should be pushed to. */
            RowCollection* records_ = nullptr;

            /** Alternate sink for speculative/private row collection. */
            CSVRowSink* row_sink_ = nullptr;

            CONSTEXPR_17 bool ws_flag(const char ch) const noexcept {
                return ws_flags_.data()[ch + CHAR_OFFSET];
            }

            size_t& current_row_start() {
                return this->current_row_.data_start;
            }

            void parse_field() noexcept;

            /** Finish parsing the current field */
            void push_field();

            /** Finish parsing the current record and reset row-local state. */
            void finish_row(size_t raw_end);

            /** Finish parsing the current row */
            void push_row(size_t raw_end);

            /** Handle possible Unicode byte order mark */
            void strip_unicode_bom();

            ParserChunkResult parse_prepared_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                const ParserChunkOptions& options
            );

            void resolve_pending_quote_at_start(csv::string_view in);
            void resolve_pending_linefeed_at_start(csv::string_view in);

            ParserDFAState current_dfa_state() const noexcept {
                return ParserDFAState{ this->quote_escape_, this->pending_quote_, this->pending_linefeed_ };
            }

            size_t finish_parse(size_t remainder) noexcept {
                this->ending_state_ = this->current_dfa_state();
                this->initial_state_ = this->ending_state_.pending_linefeed
                    ? this->ending_state_
                    : ParserDFAState{};
                this->scan_bom_for_current_chunk_ = true;
                return remainder;
            }
        };

        class ParserCoreRowSink : public CSVRowSink {
        public:
            explicit ParserCoreRowSink(CSVParserCore& parser) : parser_(parser) {}

            void push_row(CSVRow&& row) override {
                this->parser_.push_output_row(std::move(row));
            }

        private:
            CSVParserCore& parser_;
        };

        class ICSVParseOrchestrator {
        public:
            virtual ~ICSVParseOrchestrator() {}

            virtual size_t worker_count() const noexcept = 0;
            virtual SpeculativeParseDiagnostics diagnostics() const noexcept = 0;
            virtual size_t read_window_size(size_t chunk_size) const noexcept = 0;
            virtual bool utf8_bom() const noexcept = 0;
            virtual void reset_with_initial_state(ParserDFAState state) noexcept = 0;
            virtual ParserDFAState ending_state() const noexcept = 0;

            virtual CSVParseWindowResult parse_window(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                size_t base_offset,
                size_t serial_chunk_size,
                bool source_exhausted,
                CSVRowSink& output
            ) = 0;
        };

        inline std::unique_ptr<ICSVParseOrchestrator> make_csv_parse_orchestrator(
            const ParseFlagMap& parse_flags,
            const WhitespaceMap& ws_flags,
            const CSVFormat& format,
            size_t source_size,
            const ColNamesPtr& col_names = nullptr,
            bool enable_speculative_parallel = true,
            bool source_size_known = true
        );

        /** Abstract base class for source adapters.
         *
         *  It preserves the existing parser API while delegating byte-level
         *  parsing to CSVParserCore.
         */
        class CSVParserDriverBase : public CSVParserCore {
        public:
            CSVParserDriverBase() = default;
            CSVParserDriverBase(const CSVFormat&, const ColNamesPtr&);
            CSVParserDriverBase(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags
            ) : CSVParserCore(parse_flags, ws_flags) {}

            virtual ~CSVParserDriverBase() {}

            /** Whether or not we have reached the end of source */
            bool eof() { return this->eof_; }

            ResolvedFormat get_resolved_format() { return this->format; }

            /** Parse the next block of data */
            virtual void next(size_t bytes) = 0;

            virtual SpeculativeParseDiagnostics speculative_diagnostics() const noexcept {
                return SpeculativeParseDiagnostics();
            }

            virtual size_t parse_worker_count() const noexcept {
                return 1;
            }

            virtual bool utf8_bom() const noexcept {
                return CSVParserCore::utf8_bom();
            }

        protected:
            /** @name Current Stream/File State */
            ///@{
            bool eof_ = false;

            ResolvedFormat format;

            /** The size of the incoming CSV */
            size_t source_size_ = 0;
            ///@}

            virtual std::string& get_csv_head() = 0;

            void resolve_format_from_head(const CSVFormat& format);
        };
    }
}

namespace csv {
    namespace internals {
        class CSVParseOrchestrator;
        template<typename TStream>
        class StreamParser;

#if !defined(__EMSCRIPTEN__)
        /** Parser for memory-mapped files
         *
         *  @par Implementation
         *  This class constructs moving windows over a file to avoid
         *  creating massive memory maps which may require more RAM
         *  than the user has available. It contains logic to automatically
         *  re-align each memory map to the beginning of a CSV row.
         *
         *  @par Head buffer
         *  CSVReader may prime a pre-read head buffer used for format guessing
         *  via prime_head_for_reuse(). When provided, the first next() call
         *  parses that buffer directly, then resumes mmap reads from the proper
         *  file offset while preserving chunk-boundary remainder semantics.
         *
         */
        class MmapParser : public CSVParserDriverBase {
        public:
            MmapParser(csv::string_view filename,
                const CSVFormat& format,
                const ColNamesPtr& col_names = nullptr
            );

            ~MmapParser();

            std::string& get_csv_head() override {
                // head_ was already populated in the constructor.
                return this->head_;
            }

            void next(size_t bytes) override;

            SpeculativeParseDiagnostics speculative_diagnostics() const noexcept override;

            size_t parse_worker_count() const noexcept override;

            bool utf8_bom() const noexcept override;

        private:
            void finalize_loaded_chunk(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                size_t length,
                size_t chunk_size
            );

            size_t read_window_size(size_t chunk_size) const noexcept;

            std::string _filename;
            size_t mmap_pos = 0;
            std::string head_;
            std::unique_ptr<ICSVParseOrchestrator> parse_orchestrator_;
        };
#endif
    }
}
