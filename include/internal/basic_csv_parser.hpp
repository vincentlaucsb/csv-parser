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
#include <thread>
#include <vector>

#include "../external/mio.hpp"
#include "col_names.hpp"
#include "common.hpp"
#include "csv_format.hpp"
#include "csv_row.hpp"
#include "thread_safe_deque.hpp"

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

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        CSV_CONST CONSTEXPR_17 ParseFlagMap make_parse_flags(char delimiter) {
            auto ret = arrayToDefault<ParseFlagMap>(ParseFlags::NOT_SPECIAL);
            ret[delimiter + CHAR_OFFSET] = ParseFlags::DELIMITER;
            ret['\r' + CHAR_OFFSET] = ParseFlags::NEWLINE;
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

        CSV_INLINE size_t get_file_size(csv::string_view filename);

        CSV_INLINE std::string get_csv_head(csv::string_view filename);
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
            IBasicCSVParser(const ParseFlagMap& parse_flags, const WhitespaceMap& ws_flags
            ) : _parse_flags(parse_flags), _ws_flags(ws_flags) {}

            virtual ~IBasicCSVParser() {}

            /** Whether or not we have reached the end of source */
            bool eof() { return this->_eof; }

            /** Parse the next block of data */
            virtual void next(size_t bytes) = 0;

            /** Indicate the last block of data has been parsed */
            void end_feed();

            CONSTEXPR_17 ParseFlags parse_flag(const char ch) const noexcept {
                return _parse_flags.data()[ch + CHAR_OFFSET];
            }

            CONSTEXPR_17 ParseFlags compound_parse_flag(const char ch) const noexcept {
                return quote_escape_flag(parse_flag(ch), this->quote_escape);
            }

            /** Whether or not this CSV has a UTF-8 byte order mark */
            CONSTEXPR bool utf8_bom() const { return this->_utf8_bom; }

            void set_output(RowCollection& rows) { this->_records = &rows; }

        protected:
            /** @name Current Parser State */
            ///@{
            CSVRow current_row;
            RawCSVDataPtr data_ptr = nullptr;
            ColNamesPtr _col_names = nullptr;
            CSVFieldList* fields = nullptr;
            int field_start = UNINITIALIZED_FIELD;
            size_t field_length = 0;

            /** An array where the (i + 128)th slot gives the ParseFlags for ASCII character i */
            ParseFlagMap _parse_flags;
            ///@}

            /** @name Current Stream/File State */
            ///@{
            bool _eof = false;

            /** The size of the incoming CSV */
            size_t source_size = 0;
            ///@}

            /** Whether or not source needs to be read in chunks */
            CONSTEXPR bool no_chunk() const { return this->source_size < ITERATION_CHUNK_SIZE; }

            /** Parse the current chunk of data *
             *
             *  @returns How many character were read that are part of complete rows
             */
            size_t parse();

            /** Create a new RawCSVDataPtr for a new chunk of data */
            void reset_data_ptr();
        private:
            /** An array where the (i + 128)th slot determines whether ASCII character i should
             *  be trimmed
             */
            WhitespaceMap _ws_flags;
            bool quote_escape = false;
            bool field_has_double_quote = false;

            /** Where we are in the current data block */
            size_t data_pos = 0;

            /** Whether or not an attempt to find Unicode BOM has been made */
            bool unicode_bom_scan = false;
            bool _utf8_bom = false;

            /** Where complete rows should be pushed to */
            RowCollection* _records = nullptr;

            CONSTEXPR_17 bool ws_flag(const char ch) const noexcept {
                return _ws_flags.data()[ch + CHAR_OFFSET];
            }

            size_t& current_row_start() {
                return this->current_row.data_start;
            }

            void parse_field() noexcept;

            /** Finish parsing the current field */
            void push_field();

            /** Finish parsing the current row */
            void push_row();

            /** Handle possible Unicode byte order mark */
            void trim_utf8_bom();
        };

        template<typename TStream,
            csv::enable_if_t<std::is_base_of<std::istream, TStream>::value, int>  = 0>
        std::string get_csv_head(TStream &source) {
            auto tellg = source.tellg();
            std::string head;
            std::getline(source, head);
            source.seekg(tellg);
            return head;
        }

        /** Read the first 500KB of a CSV file */
        CSV_INLINE std::string get_csv_head(csv::string_view filename, size_t file_size);

        /** A class for parsing CSV data from a `std::stringstream`
         *  or an `std::ifstream`
         */
        template<typename TStream>
        class StreamParser: public IBasicCSVParser {
            using RowCollection = ThreadSafeDeque<CSVRow>;

        public:
            StreamParser(TStream& source,
                const CSVFormat& format,
                const ColNamesPtr& col_names = nullptr
            ) : IBasicCSVParser(format, col_names), _source(source) {}

            StreamParser(
                TStream& source,
                internals::ParseFlagMap parse_flags,
                internals::WhitespaceMap ws_flags) :
                IBasicCSVParser(parse_flags, ws_flags),
                _source(source)
            {}

            ~StreamParser() {}

            void next(size_t bytes = ITERATION_CHUNK_SIZE) override {
                if (this->eof()) return;

                // Reset parser state
                this->field_start = UNINITIALIZED_FIELD;
                this->field_length = 0;
                this->reset_data_ptr();
                this->data_ptr->_data = std::make_shared<std::string>();

                if (source_size == 0) {
                    const auto start = _source.tellg();
                    _source.seekg(0, std::ios::end);
                    const auto end = _source.tellg();
                    _source.seekg(0, std::ios::beg);

                    source_size = end - start;
                }

                // Read data into buffer
                size_t length = std::min(source_size - stream_pos, bytes);
                std::unique_ptr<char[]> buff(new char[length]);
                _source.seekg(stream_pos, std::ios::beg);
                _source.read(buff.get(), length);
                stream_pos = _source.tellg();
                ((std::string*)(this->data_ptr->_data.get()))->assign(buff.get(), length);

                // Create string_view
                this->data_ptr->data = *((std::string*)this->data_ptr->_data.get());

                // Parse
                this->current_row = CSVRow(this->data_ptr);
                size_t remainder = this->parse();

                if (stream_pos == source_size || no_chunk()) {
                    this->_eof = true;
                    this->end_feed();
                }
                else {
                    this->stream_pos -= (length - remainder);
                }
            }

        private:
            TStream& _source;
            size_t stream_pos = 0;
        };

        /** Parser for memory-mapped files
         *
         *  @par Implementation
         *  This class constructs moving windows over a file to avoid
         *  creating massive memory maps which may require more RAM
         *  than the user has available. It contains logic to automatically
         *  re-align each memory map to the beginning of a CSV row.
         *
         */
        class MmapParser : public IBasicCSVParser {
        public:
            MmapParser(csv::string_view filename,
                const CSVFormat& format,
                const ColNamesPtr& col_names = nullptr
            ) : IBasicCSVParser(format, col_names) {
                this->_filename = filename.data();
                this->source_size = get_file_size(filename);
            };

            ~MmapParser() {}

            void next(size_t bytes) override;

        private:
            std::string _filename;
            size_t mmap_pos = 0;
        };
    }
}
