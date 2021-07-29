/** @file
 *  @brief Contains the main CSV parsing algorithm and various utility functions
 */

#pragma once
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <vector>

#include "../external/mio.hpp"
#include "col_names.hpp"
#include "common.hpp"
#include "csv_format.hpp"
#include "csv_row.hpp"

namespace csv {
    namespace internals {
        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        HEDLEY_CONST CONSTEXPR_17 ParseFlagMap make_parse_flags(char delimiter) {
            std::array<ParseFlags, 256> ret = {};
            for (int i = -128; i < 128; i++) {
                const int arr_idx = i + 128;
                char ch = char(i);

                if (ch == delimiter)
                    ret[arr_idx] = ParseFlags::DELIMITER;
                else if (ch == '\r' || ch == '\n')
                    ret[arr_idx] = ParseFlags::NEWLINE;
                else
                    ret[arr_idx] = ParseFlags::NOT_SPECIAL;
            }

            return ret;
        }

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        HEDLEY_CONST CONSTEXPR_17 ParseFlagMap make_parse_flags(char delimiter, char quote_char) {
            std::array<ParseFlags, 256> ret = make_parse_flags(delimiter);
            ret[(size_t)quote_char + 128] = ParseFlags::QUOTE;
            return ret;
        }

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character c and, v[i + 128] is true if
         *  c is a whitespace character
         */
        HEDLEY_CONST CONSTEXPR_17 WhitespaceMap make_ws_flags(const char* ws_chars, size_t n_chars) {
            std::array<bool, 256> ret = {};
            for (int i = -128; i < 128; i++) {
                const int arr_idx = i + 128;
                char ch = char(i);
                ret[arr_idx] = false;

                for (size_t j = 0; j < n_chars; j++) {
                    if (ws_chars[j] == ch) {
                        ret[arr_idx] = true;
                    }
                }
            }

            return ret;
        }

        inline WhitespaceMap make_ws_flags(const std::vector<char>& flags) {
            return make_ws_flags(flags.data(), flags.size());
        }

        CSV_INLINE size_t get_file_size(csv::string_view filename);

        CSV_INLINE std::string get_csv_head(csv::string_view filename);

        /** Read the first 500KB of a CSV file */
        CSV_INLINE std::string get_csv_head(csv::string_view filename, size_t file_size);

        /** A std::deque wrapper which allows multiple read and write threads to concurrently
         *  access it along with providing read threads the ability to wait for the deque
         *  to become populated
         */
        template<typename T>
        class ThreadSafeDeque {
        public:
            ThreadSafeDeque(size_t notify_size = 100) : _notify_size(notify_size) {};
            ThreadSafeDeque(const ThreadSafeDeque& other) {
                this->data = other.data;
                this->_notify_size = other._notify_size;
            }

            ThreadSafeDeque(const std::deque<T>& source) : ThreadSafeDeque() {
                this->data = source;
            }

            void clear() noexcept { this->data.clear(); }

            bool empty() const noexcept {
                return this->data.empty();
            }

            T& front() noexcept {
                return this->data.front();
            }

            T& operator[](size_t n) {
                return this->data[n];
            }

            void push_back(T&& item) {
                std::lock_guard<std::mutex> lock{ this->_lock };
                this->data.push_back(std::move(item));

                if (this->size() >= _notify_size) {
                    this->_cond.notify_all();
                }
            }

            T pop_front() noexcept {
                std::lock_guard<std::mutex> lock{ this->_lock };
                T item = std::move(data.front());
                data.pop_front();
                return item;
            }

            size_t size() const noexcept { return this->data.size(); }

            /** Returns true if a thread is actively pushing items to this deque */
            constexpr bool is_waitable() const noexcept { return this->_is_waitable; }

            /** Wait for an item to become available */
            void wait() {
                if (!is_waitable()) {
                    return;
                }

                std::unique_lock<std::mutex> lock{ this->_lock };
                this->_cond.wait(lock, [this] { return this->size() >= _notify_size || !this->is_waitable(); });
                lock.unlock();
            }

            typename std::deque<T>::iterator begin() noexcept {
                return this->data.begin();
            }

            typename std::deque<T>::iterator end() noexcept {
                return this->data.end();
            }

            /** Tell listeners that this deque is actively being pushed to */
            void notify_all() {
                std::unique_lock<std::mutex> lock{ this->_lock };
                this->_is_waitable = true;
                this->_cond.notify_all();
            }

            /** Tell all listeners to stop */
            void kill_all() {
                std::unique_lock<std::mutex> lock{ this->_lock };
                this->_is_waitable = false;
                this->_cond.notify_all();
            }

        private:
            bool _is_waitable = false;
            size_t _notify_size;
            std::mutex _lock;
            std::condition_variable _cond;
            std::deque<T> data;
        };

        constexpr const int UNINITIALIZED_FIELD = -1;
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
            ) : _parse_flags(parse_flags), _ws_flags(ws_flags) {};

            virtual ~IBasicCSVParser() {}

            /** Whether or not we have reached the end of source */
            bool eof() { return this->_eof; }

            /** Parse the next block of data */
            virtual void next(size_t bytes) = 0;

            /** Indicate the last block of data has been parsed */
            void end_feed();

            CONSTEXPR_17 ParseFlags parse_flag(const char ch) const noexcept {
                return _parse_flags.data()[ch + 128];
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
                return _ws_flags.data()[ch + 128];
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
            ) : IBasicCSVParser(format, col_names), _source(std::move(source)) {};

            StreamParser(
                TStream& source,
                internals::ParseFlagMap parse_flags,
                internals::WhitespaceMap ws_flags) :
                IBasicCSVParser(parse_flags, ws_flags),
                _source(std::move(source))
            {};

            ~StreamParser() {}

            void next(size_t bytes = ITERATION_CHUNK_SIZE) override {
                if (this->eof()) return;

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
            TStream _source;
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
