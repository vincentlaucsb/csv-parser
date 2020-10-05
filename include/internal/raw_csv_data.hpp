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
#include "csv_row.hpp"

namespace csv {
    namespace internals {
        using TokenMap = std::vector<std::pair<ParseFlags, size_t>>;
        inline void tokenize_worker(TokenMap& out, csv::string_view in, ParseFlagMap parse_flags) {
            out = {};
            for (size_t i = 0; i < in.size(); i++) {
                auto flag = parse_flags[in[i] + 128];
                if (flag != ParseFlags::NOT_SPECIAL) {
                    out.push_back(std::pair(flag, i));
                }
            }
        }

        inline TokenMap tokenize(csv::string_view in, ParseFlagMap parse_flags) {
            size_t num_workers = 4;
            size_t chunk_size = in.size() / 4;

            std::vector<TokenMap> output = std::vector<TokenMap>(num_workers);
            TokenMap tokens = {};
            std::vector<std::thread> workers = {};

            for (size_t i = 0; i < num_workers; i++) {
                auto chunk = in.substr(i * chunk_size, chunk_size);
                if (i + 1 == num_workers) {
                    chunk = in.substr(i * chunk_size);
                }

                workers.push_back(std::thread(tokenize_worker, std::ref(output[i]), chunk, parse_flags));
            }

            for (auto& worker : workers) {
                worker.join();
            }

            for (auto& out : output) {
                tokens.insert(tokens.end(), out.begin(), out.end());
            }

            return tokens;
        }

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

        constexpr const size_t UNINITIALIZED_FIELD = -1;

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
            ColNamesPtr col_names = nullptr;
            RawCSVDataPtr data_ptr = nullptr;
            CSVFieldArray* fields = nullptr;
            RowCollection* _records = nullptr;

            constexpr internals::ParseFlags parse_flag(const char ch) const noexcept {
                return _parse_flags.data()[ch + 128];
            }

            constexpr internals::ParseFlags compound_parse_flag(const char ch) const noexcept {
                return internals::qe_flag(parse_flag(ch), this->quote_escape);
            }

            constexpr bool ws_flag(const char ch) const noexcept {
                return _ws_flags.data()[ch + 128];
            }

            size_t& current_row_start() {
                return this->current_row.data_start;
            }

            void push_field();

            void parse_field(string_view in, size_t& i) noexcept {
                using internals::ParseFlags;

                // Trim off leading whitespace
                while (i < in.size() && ws_flag(in[i])) i++;

                if (field_start == UNINITIALIZED_FIELD)
                    field_start = (int)(i - current_row_start());

                // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
                // sequences, use the loop below to avoid having to go through the outer
                // switch statement as much as possible
                while (i < in.size() && compound_parse_flag(in[i]) == ParseFlags::NOT_SPECIAL) i++;

                field_length = i - (field_start + current_row_start());

                // Trim off trailing whitespace, this->field_length constraint matters
                // when field is entirely whitespace
                for (size_t j = i - 1; ws_flag(in[j]) && this->field_length > 0; j--) this->field_length--;
            }

            void parse_loop(csv::string_view in);

            void push_row() {
                current_row.row_length = fields->size() - current_row.fields_start;
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