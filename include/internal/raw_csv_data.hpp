#pragma once
#include <array>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../external/mio.hpp"
#include "col_names.hpp"
#include "compatibility.hpp"
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
            ThreadSafeDeque() = default;
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
                std::unique_lock<std::mutex> lock{ this->_lock };
                this->data.push_back(std::move(item));
                this->_cond.notify_all();
                lock.unlock();
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
                this->_cond.wait(lock, [this] { return !this->empty() || !this->is_waitable(); });
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
            std::mutex _lock;
            std::condition_variable _cond;
            std::deque<T> data;
        };

        /** A class for parsing raw CSV data */
        class BasicCSVParser {
            using RowCollection = ThreadSafeDeque<CSVRow>;

        public:
            BasicCSVParser() = default;
            BasicCSVParser(internals::ColNamesPtr _col_names) : col_names(_col_names) {};
            BasicCSVParser(internals::ParseFlagMap parse_flags, internals::WhitespaceMap ws_flags) :
                _parse_flags(parse_flags), _ws_flags(ws_flags) {};

            void parse(mio::mmap_source&& source) {
                this->data_source = std::move(source);
                this->parse(csv::string_view(this->data_source.data(), this->data_source.length()));
            }

            void parse(csv::string_view in);
            void parse(csv::string_view in, RowCollection& records) {
                this->set_output(records);
                this->parse(in);
            }
        
            void end_feed() {
                using internals::ParseFlags;

                bool empty_last_field = this->current_row.data
                    && !this->current_row.data->data.empty()
                    && parse_flag(this->current_row.data->data.back()) == ParseFlags::DELIMITER;

                // Push field
                if (this->field_length > 0 || empty_last_field) {
                    this->push_field();
                }

                // Push row
                if (this->current_row.size() > 0) 
                    this->push_row();
            }

            void set_output(RowCollection& records) { this->_records = &records; }

            void set_parse_flags(internals::ParseFlagMap parse_flags) {
                _parse_flags = parse_flags;
            }

            void set_ws_flags(internals::WhitespaceMap ws_flags) {
                _ws_flags = ws_flags;
            }

        private:
            /** An array where the (i + 128)th slot gives the ParseFlags for ASCII character i */
            internals::ParseFlagMap _parse_flags;

            /** An array where the (i + 128)th slot determines whether ASCII character i should
             *  be trimmed
             */
            internals::WhitespaceMap _ws_flags;

            CSVRow current_row;
            bool quote_escape = false;
            int field_start = -1;
            size_t field_length = 0;
            bool field_has_double_quote = false;
            mio::mmap_source data_source;

            internals::ColNamesPtr col_names = nullptr;
            RawCSVDataPtr data_ptr = nullptr;
            internals::CSVFieldArray* fields = nullptr;
            RowCollection* _records = nullptr;

            constexpr internals::ParseFlags parse_flag(const char ch) const noexcept {
                return _parse_flags.data()[ch + 128];
            }

            constexpr internals::CompoundParseFlags compound_parse_flag(const char ch) const noexcept {
                return internals::compound_flag(parse_flag(ch), this->quote_escape);
            }

            constexpr bool ws_flag(const char ch) const noexcept {
                return _ws_flags.data()[ch + 128];
            }

            size_t& current_row_start() {
                return this->current_row.data_start;
            }

            void push_field();

            template<bool QuoteEscape=false>
            CONSTEXPR void parse_field(string_view in, size_t& i) noexcept {
                using internals::ParseFlags;

                // Trim off leading whitespace
                while (i < in.size() && ws_flag(in[i])) i++;

                if (this->field_start < 0) {
                    this->field_start = (int)(i - current_row_start());
                }

                // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
                // sequences, use the loop below to avoid having to go through the outer
                // switch statement as much as possible
                IF_CONSTEXPR(QuoteEscape) {
                    while (i < in.size() && parse_flag(in[i]) != ParseFlags::QUOTE) i++;
                }
                else {
                    while (i < in.size() && parse_flag(in[i]) == ParseFlags::NOT_SPECIAL) i++;
                }

                this->field_length = i - (this->field_start + current_row_start());

                // Trim off trailing whitespace, this->field_length constraint matters
                // when field is entirely whitespace
                for (size_t j = i - 1; ws_flag(in[j]) && this->field_length > 0; j--) this->field_length--;
            }

            void parse_loop(csv::string_view in);

            void push_row() {
                current_row.row_length = current_row.data->fields.size() - current_row.field_bounds_index;
                this->_records->push_back(std::move(current_row));
            };

            void set_data_ptr(RawCSVDataPtr ptr) {
                this->data_ptr = ptr;
                this->data_ptr->parse_flags = this->_parse_flags;
                this->fields = &(ptr->fields);
            }
        };
    }
}