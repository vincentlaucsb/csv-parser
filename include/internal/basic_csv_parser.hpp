#pragma once
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

            IBasicCSVParser(internals::ColNamesPtr _col_names) : col_names(_col_names) {};

            IBasicCSVParser(internals::ParseFlagMap parse_flags, internals::WhitespaceMap ws_flags) :
                _parse_flags(parse_flags), _ws_flags(ws_flags) {};

            virtual bool eof() = 0;
            virtual void next() = 0;

            constexpr internals::ParseFlags parse_flag(const char ch) const noexcept {
                return _parse_flags.data()[ch + 128];
            }

            constexpr internals::ParseFlags compound_parse_flag(const char ch) const noexcept {
                return internals::qe_flag(parse_flag(ch), this->quote_escape);
            }

            void set_output(RowCollection& records) { this->_records = &records; }

            template<typename TSource>
            void set_data_source(TSource&& source) {
                this->data_ptr = std::make_shared<RawCSVData>();
                this->data_ptr->_data = std::make_shared<TSource>(std::move(source));
                this->data_ptr->data = csv::string_view(*((TSource*)this->data_ptr->_data.get()));
                this->data_ptr->parse_flags = this->_parse_flags;
                this->fields = &(this->data_ptr->fields);

                this->current_row = CSVRow(this->data_ptr);
            }

            template<>
            void set_data_source(csv::string_view& source) {
                this->data_ptr = std::make_shared<RawCSVData>();
                this->data_ptr->data = source;
                this->data_ptr->parse_flags = this->_parse_flags;
                this->fields = &(this->data_ptr->fields);

                this->current_row = CSVRow(this->data_ptr);
            }

            template<>
            void set_data_source(mio::basic_mmap_source<char>&& source) {
                this->data_ptr = std::make_shared<RawCSVData>();
                this->data_ptr->_data = std::make_shared<mio::basic_mmap_source<char>>(std::move(source));

                auto mmap_ptr = (mio::basic_mmap_source<char>*)(this->data_ptr->_data.get());

                this->data_ptr->data = csv::string_view(
                    mmap_ptr->data(), mmap_ptr->length()
                );
                this->data_ptr->parse_flags = this->_parse_flags;
                this->fields = &(this->data_ptr->fields);

                this->current_row = CSVRow(this->data_ptr);
            }

            /**
            template<>
            void set_data_source(std::stringstream&& source) {
                this->data_ptr = std::make_shared<RawCSVData>();
                this->data_ptr->_data = std::make_shared<std::stringstream>(std::move(source));
                this->data_ptr->data = csv::string_view(
                    ((std::stringstream*)(this->data_ptr->_data.get()))->str()
                );
                this->data_ptr->parse_flags = this->_parse_flags;
                this->fields = &(this->data_ptr->fields);

                this->current_row = CSVRow(this->data_ptr);
            }
            **/

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

        protected:
            RawCSVDataPtr data_ptr = nullptr;
            int field_start = UNINITIALIZED_FIELD;
            size_t field_length = 0;

            void push_field();

            CSVRow current_row;
            void push_row() {
                current_row.row_length = fields->size() - current_row.fields_start;
                this->_records->push_back(std::move(current_row));
            };

            size_t parse_loop(csv::string_view in);

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

            mio::mmap_source data_source;
            ColNamesPtr col_names = nullptr;
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
        template<typename TSource>
        class basic_csv_parser: public IBasicCSVParser {
            using RowCollection = ThreadSafeDeque<CSVRow>;

        public:
            basic_csv_parser(internals::ParseFlagMap parse_flags, internals::WhitespaceMap ws_flags) : IBasicCSVParser(parse_flags, ws_flags) {};

            void next() override {
                this->current_row = CSVRow(this->data_ptr);
                this->parse_loop(this->data_ptr->data);
                this->end_feed();
            }

            bool eof() override { return true; }
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

            bool eof() override {
                return this->mmap_eof;
            }

            void next() override {
                size_t length = std::min(this->file_size - this->mmap_pos, csv::internals::ITERATION_CHUNK_SIZE);
                std::error_code error;
                auto _csv_mmap = mio::make_mmap_source(this->_filename, this->mmap_pos, length, error);

                if (error) throw error;

                this->mmap_pos += length;

                this->field_start = UNINITIALIZED_FIELD;
                this->field_length = 0;

                this->set_data_source(std::move(_csv_mmap));
                size_t remainder = this->parse_loop(this->data_ptr->data);

                if (remainder > 0) {
                    this->mmap_pos -= (length - remainder);
                }

                if (this->mmap_pos == this->file_size) {
                    this->mmap_eof = true;
                    this->end_feed();
                }
            }

        private:
            std::string _filename;
            size_t file_size = 0;
            bool mmap_eof = false;
            size_t mmap_pos = 0;
        };
    }
}