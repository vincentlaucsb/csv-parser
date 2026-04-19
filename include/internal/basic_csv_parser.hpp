/** @file
 *  @brief Contains the main CSV parsing algorithm and various utility functions
 */

#pragma once
#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#include "../external/mio.hpp"
#endif
#include "basic_csv_parser_simd.hpp"
#include "col_names.hpp"
#include "common.hpp"
#include "csv_format.hpp"
#include "csv_row.hpp"
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

        /** Read the first 500KB from a filename using stream I/O. */
        CSV_INLINE std::string get_csv_head_stream(csv::string_view filename);

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

        class IBasicCSVParser;
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
        /** Abstract base class which provides CSV parsing logic.
         *
         *  Concrete implementations may customize this logic across
         *  different input sources, such as memory mapped files, stringstreams,
         *  etc...
         */
        class IBasicCSVParser {
        public:
            IBasicCSVParser() = default;
            IBasicCSVParser(const CSVFormat&, const ColNamesPtr&);
            IBasicCSVParser(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags
            ) : parse_flags_(parse_flags), ws_flags_(ws_flags) {
                const char d = internals::infer_delimiter(parse_flags);
                simd_sentinels_ = SentinelVecs(d, internals::infer_quote_char(parse_flags, d));
                has_ws_trimming_ = std::any_of(ws_flags.begin(), ws_flags.end(), [](bool b) { return b; });
            }

            virtual ~IBasicCSVParser() {}

            /** Whether or not we have reached the end of source */
            bool eof() { return this->eof_; }

            ResolvedFormat get_resolved_format() { return this->format; }

            /** Parse the next block of data */
            virtual void next(size_t bytes) = 0;

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

            void set_output(RowCollection& rows) { this->records_ = &rows; }

        protected:
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

            /** @name Current Stream/File State */
            ///@{
            bool eof_ = false;

            ResolvedFormat format;

            /** The size of the incoming CSV */
            size_t source_size_ = 0;
            ///@}

            virtual std::string& get_csv_head() = 0;

            /** Whether or not source needs to be read in chunks */
            CONSTEXPR bool no_chunk() const { return this->source_size_ < CSV_CHUNK_SIZE_DEFAULT; }

            /** Parse the current chunk of data and return the completed-row prefix length. */
            size_t parse();

            /** Create a new RawCSVDataPtr for a new chunk of data */
            void reset_data_ptr();

            void resolve_format_from_head(CSVFormat format);
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
            bool field_has_double_quote_ = false;

            /** Where we are in the current data block */
            size_t data_pos_ = 0;

            /** Whether or not an attempt to find Unicode BOM has been made */
            bool unicode_bom_scan_ = false;
            bool utf8_bom_ = false;

            /** Where complete rows should be pushed to */
            RowCollection* records_ = nullptr;

            CONSTEXPR_17 bool ws_flag(const char ch) const noexcept {
                return ws_flags_.data()[ch + CHAR_OFFSET];
            }

            size_t& current_row_start() {
                return this->current_row_.data_start;
            }

            void parse_field() noexcept;

            /** Finish parsing the current field */
            void push_field();

            /** Finish parsing the current row */
            void push_row();

            /** Handle possible Unicode byte order mark */
            void trim_utf8_bom();
        };

        /** A class for parsing CSV data from any std::istream, including
         *  non-seekable sources such as pipes and decompression filters.
         *
         *  @par Chunk boundary handling
         *  parse() returns the byte offset of the start of the last incomplete
         *  row in the current chunk (the "remainder"). Rather than seeking back
         *  to re-read those bytes (which requires a seekable stream), they are
         *  saved in `leftover_` and prepended to the next chunk. This is
         *  semantically identical to the old seek-back approach but works on
         *  any istream and avoids the syscall overhead of seekg().
         *
         *  @par Format resolution
         *  The constructor reads a head buffer from the stream via get_csv_head()
         *  and passes it to resolve_format_from_head(), which infers the delimiter
         *  and header row when not explicitly set. The head bytes are stored in
         *  `leftover_` so the first next() call re-parses them without re-reading.
         */
        template<typename TStream>
        class StreamParser: public IBasicCSVParser {
            using RowCollection = ThreadSafeDeque<CSVRow>;

        public:
            StreamParser(TStream& source,
                const CSVFormat& format,
                const ColNamesPtr& col_names = nullptr
            ) : IBasicCSVParser(format, col_names),
                source_(source) {
                this->resolve_format_from_head(format);
            }

            StreamParser(
                TStream& source,
                internals::ParseFlagMap parse_flags,
                internals::WhitespaceMap ws_flags) :
                IBasicCSVParser(parse_flags, ws_flags),
                source_(source)
            {}

            ~StreamParser() {}

            std::string& get_csv_head() override {
                leftover_ = get_csv_head_stream(this->source_);
                return this->leftover_;
            }

            void next(size_t bytes = CSV_CHUNK_SIZE_DEFAULT) override {
                if (this->eof()) return;

                // Reset parser state
                this->field_start_ = UNINITIALIZED_FIELD;
                this->field_length_ = 0;
                this->reset_data_ptr();
                this->data_ptr_->_data = std::make_shared<std::string>();

                auto& chunk = *static_cast<std::string*>(this->data_ptr_->_data.get());

                // Prepend leftover bytes from the previous chunk's incomplete
                // trailing row, then read the next block from the stream.
                // Uses a raw buffer to avoid std::string::resize() zero-fill
                // on the full 10MB chunk size (critical for tiny inputs).
                chunk = std::move(leftover_);
                std::unique_ptr<char[]> buf(new char[bytes]);
                source_.read(buf.get(), (std::streamsize)bytes);

                const size_t n = static_cast<size_t>(source_.gcount());
                
                if (n > 0) chunk.append(buf.get(), n);

                // Check for real I/O errors only (bad bit indicates unrecoverable error).
                // failbit alone is not fatal - it's set on EOF or when requesting bytes
                // beyond available data, which is normal behavior for stringstreams.
                if (source_.bad()) {
                    throw std::runtime_error("StreamParser read failure");
                }

                // Create string_view
                this->data_ptr_->data = chunk;

                // Parse
                this->current_row_ = CSVRow(this->data_ptr_);
                size_t remainder = this->parse();

                if (source_.eof() || chunk.empty()) {
                    this->eof_ = true;
                    this->end_feed();
                }
                else {
                    // Save the tail bytes that begin an incomplete row so they
                    // are prepended to the next chunk (see class-level comment).
                    leftover_ = chunk.substr(remainder);
                }
            }

        private:
            // Bytes from the previous chunk that form the start of an incomplete
            // row, plus the initial head buffer on the first call.
            std::string leftover_;

            TStream& source_;
        };

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
        class MmapParser : public IBasicCSVParser {
        public:
            MmapParser(csv::string_view filename,
                const CSVFormat& format,
                const ColNamesPtr& col_names = nullptr
            ) : IBasicCSVParser(format, col_names) {
                this->_filename = filename.data();
                auto head_and_size = get_csv_head_mmap(filename);
                this->head_ = std::move(head_and_size.first);
                this->source_size_ = head_and_size.second;
                this->resolve_format_from_head(format);
            };

            ~MmapParser() {}

            std::string& get_csv_head() override {
                // head_ was already populated in the constructor.
                return this->head_;
            }

            void next(size_t bytes) override;

        private:
            void finalize_loaded_chunk(size_t length, bool eof_on_no_chunk = false);

            std::string _filename;
            size_t mmap_pos = 0;
            std::string head_;
        };
#endif
    }
}
