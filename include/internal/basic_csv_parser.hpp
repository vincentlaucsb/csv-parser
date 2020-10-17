#pragma once
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <thread>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../external/mio.hpp"
#include "col_names.hpp"
#include "compatibility.hpp"
#include "csv_format.hpp"
#include "csv_reader_internals.hpp"
#include "csv_row.hpp"

namespace csv {
    namespace internals {
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
            }

            ThreadSafeDeque(const std::deque<T>& source) {
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

        class IBasicCSVParser {
            using RowCollection = ThreadSafeDeque<CSVRow>;

        public:
            IBasicCSVParser() = default;

            IBasicCSVParser(internals::ParseFlagMap parse_flags, internals::WhitespaceMap ws_flags) :
                _parse_flags(parse_flags), _ws_flags(ws_flags) {};

            virtual ~IBasicCSVParser() {

            }

            bool eof() { return this->_eof; }
            virtual void next() = 0;

            constexpr internals::ParseFlags parse_flag(const char ch) const noexcept {
                return _parse_flags.data()[ch + 128];
            }

            constexpr internals::ParseFlags compound_parse_flag(const char ch) const noexcept {
                return internals::qe_flag(parse_flag(ch), this->quote_escape);
            }

            void set_output(RowCollection& records) { this->_records = &records; }

            void set_col_names(const ColNamesPtr& _col_names) {
                this->col_names = _col_names;
            }

            void end_feed() {
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
            
            CONSTEXPR bool utf8_bom() const {
                return this->_utf8_bom;
            }

        protected:
            bool _eof = false;
            RawCSVDataPtr data_ptr = nullptr;
            int field_start = UNINITIALIZED_FIELD;
            size_t field_length = 0;

            void push_field();

            CSVRow current_row;

            void trim_utf8_bom() {
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

            void push_row() {
                current_row.row_length = fields->size() - current_row.fields_start;
                this->_records->push_back(std::move(current_row));
            };

            void reset_data_ptr() {
                this->data_ptr = std::make_shared<RawCSVData>();
                this->data_ptr->parse_flags = this->_parse_flags;
                this->data_ptr->col_names = this->col_names;
                this->fields = &(this->data_ptr->fields);
            }

            size_t parse_loop();

            ColNamesPtr col_names = nullptr;
            CSVFieldArray* fields = nullptr;

            /** An array where the (i + 128)th slot gives the ParseFlags for ASCII character i */
            internals::ParseFlagMap _parse_flags;

        private:
            /** An array where the (i + 128)th slot determines whether ASCII character i should
             *  be trimmed
             */
            internals::WhitespaceMap _ws_flags;
            bool quote_escape = false;
            bool field_has_double_quote = false;

            /** Whether or not an attempt to find Unicode BOM has been made */
            bool unicode_bom_scan = false;
            bool _utf8_bom = false;

            RowCollection* _records = nullptr;

            constexpr bool ws_flag(const char ch) const noexcept {
                return _ws_flags.data()[ch + 128];
            }

            size_t& current_row_start() {
                return this->current_row.data_start;
            }
                
            void parse_field(csv::string_view in, size_t& i) noexcept;
        };

        /** A class for parsing raw CSV data */
        template<typename TStream>
        class BasicStreamParser: public IBasicCSVParser {
            using RowCollection = ThreadSafeDeque<CSVRow>;

        public:
            BasicStreamParser(
                TStream& source,
                internals::ParseFlagMap parse_flags,
                internals::WhitespaceMap ws_flags) :
                IBasicCSVParser(parse_flags, ws_flags),
                _source(std::move(source))
            {};

            ~BasicStreamParser() {}

            void next() override {
                this->reset_data_ptr();
                this->data_ptr->_data = std::make_shared<std::string>();

                if (stream_length == 0) {
                    const auto start = _source.tellg();
                    _source.seekg(0, std::ios::end);
                    const auto end = _source.tellg();
                    _source.seekg(0, std::ios::beg);

                    stream_length = end - start;
                }

                // Read data into buffer
                size_t length = std::min(stream_length - stream_pos, internals::ITERATION_CHUNK_SIZE);
                std::unique_ptr<char[]> buff(new char[length]);
                _source.read(buff.get(), length);
                ((std::string*)(this->data_ptr->_data.get()))->assign(buff.get(), length);

                stream_pos += length;

                // Create string_view
                this->data_ptr->data = *((std::string*)this->data_ptr->_data.get());

                if (_source.eof() || stream_pos == stream_length) {
                    this->_eof = true;
                }

                this->current_row = CSVRow(this->data_ptr);
                this->parse_loop();
                this->end_feed();
            }

        private:
            TStream _source;
            size_t stream_pos = 0;
            size_t stream_length = 0;
        };

        class BasicMmapParser : public IBasicCSVParser {
        public:
            BasicMmapParser(
                csv::string_view filename,
                internals::ParseFlagMap parse_flags,
                internals::WhitespaceMap ws_flags
            ) : IBasicCSVParser(parse_flags, ws_flags) {
                this->_filename = filename;
                this->file_size = internals::get_file_size(filename);
            };

            ~BasicMmapParser() {}

            void next() override {
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

        private:
            std::string _filename;
            size_t file_size = 0;
            size_t mmap_pos = 0;
        };
    }
}