/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#include "csv_reader.hpp"
#include "csv_reader.hpp"
#include "csv_reader.hpp"
#include <algorithm>
#include <cstdio>   // For read_csv()
#include <cstring>  // For read_csv()
#include <fstream>
#include <sstream>

#include "constants.hpp"
#include "csv_reader.hpp"

namespace csv {
    namespace internals {
        std::string format_row(const std::vector<std::string>& row, csv::string_view delim) {
            /** Print a CSV row */
            std::stringstream ret;
            for (size_t i = 0; i < row.size(); i++) {
                ret << row[i];
                if (i + 1 < row.size()) ret << delim;
                else ret << std::endl;
            }

            return ret.str();
        }

        //
        // CSVGuesser
        //
        void CSVGuesser::Guesser::bad_row_handler(std::vector<std::string> record) {
            /** Helps CSVGuesser tally up the size of rows encountered while parsing */
            if (row_tally.find(record.size()) != row_tally.end()) row_tally[record.size()]++;
            else {
                row_tally[record.size()] = 1;
                row_when[record.size()] = this->row_num + 1;
            }
        }

        CSVGuessResult CSVGuesser::guess_delim() {
            /** Guess the delimiter of a CSV by scanning the first 100 lines by
            *  First assuming that the header is on the first row
            *  If the first guess returns too few rows, then we move to the second
            *  guess method
            */
            CSVFormat format;
            if (!first_guess()) second_guess();

            return { delim, header_row };
        }

        bool CSVGuesser::first_guess() {
            /** Guess the delimiter of a delimiter separated values file
             *  by scanning the first 100 lines
             *
             *  - "Winner" is based on which delimiter has the most number
             *    of correctly parsed rows + largest number of columns
             *  -  **Note:** Assumes that whatever the dialect, all records
             *     are newline separated
             *
             *  Returns True if guess was a good one and second guess isn't needed
             */

            CSVFormat format;
            char current_delim{ ',' };
            RowCount max_rows = 0,
                temp_rows = 0;
            size_t max_cols = 0;

            // Read first 500KB of the CSV file
            this->get_csv_head();

            for (char cand_delim: this->delims) {
                format.delimiter(cand_delim);
                CSVReader guesser(format);
                guesser.feed(this->head);
                guesser.end_feed();

                // WORKAROUND on Unix systems because certain newlines
                // get double counted
                // temp_rows = guesser.correct_rows;
                temp_rows = std::min(guesser.correct_rows, (RowCount)100);
                if ((guesser.row_num >= max_rows) &&
                    (guesser.get_col_names().size() > max_cols)) {
                    max_rows = temp_rows;
                    max_cols = guesser.get_col_names().size();
                    current_delim = cand_delim;
                }
            }

            this->delim = current_delim;

            // If there are only a few rows/columns, trying guessing again
            return (max_rows > 10 && max_cols > 2);
        }

        void CSVGuesser::second_guess() {
            /** For each delimiter, find out which row length was most common.
             *  The delimiter with the longest mode row length wins.
             *  Then, the line number of the header row is the first row with
             *  the mode row length.
             */

            CSVFormat format;
            size_t max_rlen = 0,
                header = 0;

            for (char cand_delim: this->delims) {
                format.delimiter(cand_delim);
                Guesser guess(format);
                guess.feed(this->head);
                guess.end_feed();

                // Most common row length
                auto max = std::max_element(guess.row_tally.begin(), guess.row_tally.end(),
                    [](const std::pair<size_t, size_t>& x,
                        const std::pair<size_t, size_t>& y) {
                    return x.second < y.second; });

                // Idea: If CSV has leading comments, actual rows don't start
                // until later and all actual rows get rejected because the CSV
                // parser mistakenly uses the .size() of the comment rows to
                // judge whether or not they are valid.
                // 
                // The first part of the if clause means we only change the header
                // row if (number of rejected rows) > (number of actual rows)
                if (max->second > guess.records.size() &&
                    (max->first > max_rlen)) {
                    max_rlen = max->first;
                    header = guess.row_when[max_rlen];
                }
            }

            this->header_row = static_cast<int>(header);
        }

        /** Read the first 500KB of a CSV file */
        void CSVGuesser::get_csv_head() {
            const size_t bytes = 500000;
            std::ifstream infile(this->filename);
            if (!infile.is_open()) {
                throw std::runtime_error("Cannot open file " + this->filename);
            }

            std::unique_ptr<char[]> buffer(new char[bytes + 1]);
            char * head_buffer = buffer.get();

            for (size_t i = 0; i < bytes + 1; i++) {
                head_buffer[i] = '\0';
            }

            infile.read(head_buffer, bytes);
            this->head = head_buffer;
        }
    }

    /** Guess the delimiter used by a delimiter-separated values file */
    CSVGuessResult guess_format(csv::string_view filename, const std::vector<char>& delims) {
        internals::CSVGuesser guesser(filename, delims);
        return guesser.guess_delim();
    }

    HEDLEY_CONST CONSTEXPR
    std::array<CSVReader::ParseFlags, 256> CSVReader::make_parse_flags() const {
        std::array<ParseFlags, 256> ret = {};
        for (int i = -128; i < 128; i++) {
            const int arr_idx = i + 128;
            char ch = char(i);

            if (ch == this->delimiter)
                ret[arr_idx] = DELIMITER;
            else if (ch == this->quote_char)
                ret[arr_idx] = QUOTE;
            else if (ch == '\r' || ch == '\n')
                ret[arr_idx] = NEWLINE;
            else
                ret[arr_idx] = NOT_SPECIAL;
        }

        return ret;
    }

    HEDLEY_CONST CONSTEXPR
    std::array<bool, 256> CSVReader::make_ws_flags(const char * delims, size_t n_chars) const {
        std::array<bool, 256> ret = {};
        for (int i = -128; i < 128; i++) {
            const int arr_idx = i + 128;
            char ch = char(i);
            ret[arr_idx] = false;

            for (size_t j = 0; j < n_chars; j++) {
                if (delims[j] == ch) {
                    ret[arr_idx] = true;
                }
            }
        }

        return ret;
    }

    void CSVReader::bad_row_handler(std::vector<std::string> record) {
        /** Handler for rejected rows (too short or too long). This does nothing
         *  unless strict parsing was set, in which case it throws an eror.
         *  Subclasses of CSVReader may easily override this to provide
         *  custom behavior.
         */
        if (this->strict) {
            std::string problem;
            if (record.size() > this->col_names->size()) problem = "too long";
            else problem = "too short";

            throw std::runtime_error("Line " + problem + " around line " +
                std::to_string(correct_rows) + " near\n" +
                internals::format_row(record)
            );
        }
    };

    /** Allows parsing in-memory sources (by calling feed() and end_feed()). */
    CSVReader::CSVReader(CSVFormat format) :
        delimiter(format.get_delim()), quote_char(format.quote_char),
        header_row(format.header), strict(format.strict),
        unicode_bom_scan(!format.unicode_detect) {
        if (!format.col_names.empty()) {
            this->set_col_names(format.col_names);
        }
        
        parse_flags = this->make_parse_flags();
        ws_flags = this->make_ws_flags(format.trim_chars.data(), format.trim_chars.size());
    };

    /** Allows reading a CSV file in chunks, using overlapped
     *  threads for simulatenously reading from disk and parsing.
     *  Rows should be retrieved with read_row() or by using
     *  CSVReader::iterator.
     *
     *  **Details:** Reads the first 500kB of a CSV file to infer file information
     *              such as column names and delimiting character.
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     *  \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
    CSVReader::CSVReader(csv::string_view filename, CSVFormat format) {
        /** Guess delimiter and header row */
        if (format.guess_delim()) {
            auto guess_result = guess_format(filename, format.possible_delimiters);
            format.delimiter(guess_result.delim);
            format.header = guess_result.header_row;
        }

        if (!format.col_names.empty()) {
            this->set_col_names(format.col_names);
        }

        header_row = format.header;
        delimiter = format.get_delim();
        quote_char = format.quote_char;
        strict = format.strict;
        parse_flags = this->make_parse_flags();
        ws_flags = this->make_ws_flags(format.trim_chars.data(), format.trim_chars.size());

        // Read first 500KB of CSV
        this->fopen(filename);
        this->read_csv(500000);
    }

    /** Return the format of the original raw CSV */
    CSVFormat CSVReader::get_format() const {
        CSVFormat format;
        format.delimiter(this->delimiter)
            .quote(this->quote_char);

        // Since users are normally not allowed to set 
        // column names and header row simulatenously,
        // we will set the backing variables directly here
        format.col_names = this->col_names->col_names;
        format.header = this->header_row;

        return format;
    }

    /** Return the CSV's column names as a vector of strings. */
    std::vector<std::string> CSVReader::get_col_names() const {
        return this->col_names->get_col_names();
    }

    /** Return the index of the column name if found or
     *         csv::CSV_NOT_FOUND otherwise.
     */
    int CSVReader::index_of(csv::string_view col_name) const {
        auto _col_names = this->get_col_names();
        for (size_t i = 0; i < _col_names.size(); i++)
            if (_col_names[i] == col_name) return (int)i;

        return CSV_NOT_FOUND;
    }

    void CSVReader::feed(WorkItem&& buff) {
        this->feed( csv::string_view(buff.first.get(), buff.second) );
    }

    void CSVReader::feed(csv::string_view in) {
        /** Parse a CSV-formatted string.
         *
         *  @par Usage
         *  Incomplete CSV fragments can be joined together by calling feed() on them sequentially.
         *  
         *  @note
         *  `end_feed()` should be called after the last string.
         */

        this->handle_unicode_bom(in);
        bool quote_escape = false;  // Are we currently in a quote escaped field?

        // Optimizations
        auto * HEDLEY_RESTRICT _parse_flags = this->parse_flags.data();
        auto * HEDLEY_RESTRICT _ws_flags = this->ws_flags.data();
        auto& row_buffer = *(this->record_buffer.get());
        auto& text_buffer = row_buffer.buffer;
        auto& split_buffer = row_buffer.split_buffer;
        text_buffer.reserve(in.size());
        split_buffer.reserve(in.size() / 10);

        const size_t in_size = in.size();
        for (size_t i = 0; i < in_size; i++) {
            switch (_parse_flags[in[i] + 128]) {
                case DELIMITER:
                    if (!quote_escape) {
                        split_buffer.push_back((unsigned short)row_buffer.size());
                        break;
                    }

                    HEDLEY_FALL_THROUGH;
                case NEWLINE:
                    if (!quote_escape) {
                        // End of record -> Write record
                        if (i + 1 < in_size && in[i + 1] == '\n') // Catches CRLF (or LFLF)
                            ++i;
                        this->write_record();
                        break;
                    }

                    // Treat as regular character
                    text_buffer += in[i];
                    break;
                case NOT_SPECIAL: {
                    size_t start, end;

                    // Trim off leading whitespace
                    while (i < in_size && _ws_flags[in[i] + 128]) {
                        i++;
                    }

                    start = i;

                    // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
                    // sequences, use the loop below to avoid having to go through the outer
                    // switch statement as much as possible
                    while (i + 1 < in_size && _parse_flags[in[i + 1] + 128] == NOT_SPECIAL) {
                        i++;
                    }

                    // Trim off trailing whitespace
                    end = i;
                    while (_ws_flags[in[end] + 128]) {
                        end--;
                    }

                    // Finally append text
#ifdef CSV_HAS_CXX17
                    text_buffer += in.substr(start, end - start + 1);
#else
                    for (; start < end + 1; start++) {
                        text_buffer += in[start];
                    }
#endif

                    break;
                }
                default: // Quote
                    if (!quote_escape) {
                        // Don't deref past beginning
                        if (i && _parse_flags[in[i - 1] + 128] >= DELIMITER) {
                            // Case: Previous character was delimiter or newline
                            quote_escape = true;
                        }

                        break;
                    }

                    auto next_ch = _parse_flags[in[i + 1] + 128];
                    if (next_ch >= DELIMITER) {
                        // Case: Delim or newline => end of field
                        quote_escape = false;
                        break;
                    }
                        
                    // Case: Escaped quote
                    text_buffer += in[i];

                    if (next_ch == QUOTE)
                        ++i;  // Case: Two consecutive quotes
                    else if (this->strict)
                        throw std::runtime_error("Unescaped single quote around line " +
                            std::to_string(this->correct_rows) + " near:\n" +
                            std::string(in.substr(i, 100)));
                        
                    break;
            }
        }
        
        this->record_buffer = row_buffer.reset();
    }

    void CSVReader::end_feed() {
        /** Indicate that there is no more data to receive,
         *  and handle the last row
         */
        this->write_record();
    }

    CONSTEXPR void CSVReader::handle_unicode_bom(csv::string_view& in) {
        if (!this->unicode_bom_scan) {
            if (in[0] == '\xEF' && in[1] == '\xBB' && in[2] == '\xBF') {            
                in.remove_prefix(3); // Remove BOM from input string
                this->utf8_bom = true;
            }

            this->unicode_bom_scan = true;
        }
    }

    void CSVReader::write_record() {
        /** Push the current row into a queue if it is the right length.
         *  Drop it otherwise.
         */

        if (header_was_parsed) {
            // Make sure record is of the right length
            const size_t row_size = this->record_buffer->splits_size();
            if (row_size + 1 == this->n_cols) {
                this->correct_rows++;
                this->records.push_back(CSVRow(this->record_buffer));
            }
            else {
                /* 1) Zero-length record, probably caused by extraneous newlines
                 * 2) Too short or too long
                 */
                this->row_num--;
                if (row_size > 0)
                    bad_row_handler(std::vector<std::string>(CSVRow(
                        this->record_buffer)));
            }
        }
        else if (this->row_num == this->header_row) {
            this->set_col_names(std::vector<std::string>(CSVRow(this->record_buffer)));
        } // else: Ignore rows before header row

        this->row_num++;
    }

    void CSVReader::read_csv_worker() {
        /** Worker thread for read_csv() which parses CSV rows (while the main
         *         thread pulls data from disk)
         */
        while (true) {
            std::unique_lock<std::mutex> lock{ this->feed_lock }; // Get lock
            this->feed_cond.wait(lock,                            // Wait
                [this] { return !(this->feed_buffer.empty()); });

            // Wake-up
            auto in = std::move(this->feed_buffer.front());
            this->feed_buffer.pop_front();

            // Nullptr --> Die
            if (!in.first) break;

            lock.unlock();      // Release lock
            this->feed(std::move(in));
        }
    }

    void CSVReader::fopen(csv::string_view filename) {
        if (!this->infile) {
#ifdef _MSC_BUILD
            // Silence compiler warnings in Microsoft Visual C++
            size_t err = fopen_s(&(this->infile), filename.data(), "rb");
            if (err)
                throw std::runtime_error("Cannot open file " + std::string(filename));
#else
            this->infile = std::fopen(filename.data(), "rb");
            if (!this->infile)
                throw std::runtime_error("Cannot open file " + std::string(filename));
#endif
        }
    }

    /**
     *  @param[in] names Column names
     */
    void CSVReader::set_col_names(const std::vector<std::string>& names)
    {
        this->col_names = std::make_shared<internals::ColNames>(names);
        this->record_buffer->col_names = this->col_names;
        this->header_was_parsed = true;
        this->n_cols = names.size();
    }

    /**
     * Parse a CSV file using multiple threads
     *
     * @pre CSVReader::infile points to a valid file handle, i.e. CSVReader::fopen was called
     *
     * @param[in] bytes Number of bytes to read.
     * @see CSVReader::read_row()
     */
    void CSVReader::read_csv(const size_t& bytes) {
        const size_t BUFFER_UPPER_LIMIT = std::min(bytes, (size_t)1000000);
        std::unique_ptr<char[]> buffer(new char[BUFFER_UPPER_LIMIT]);
        auto * HEDLEY_RESTRICT line_buffer = buffer.get();
        line_buffer[0] = '\0';

        std::thread worker(&CSVReader::read_csv_worker, this);

        for (size_t processed = 0; processed < bytes; ) {
            char * HEDLEY_RESTRICT result = std::fgets(line_buffer, internals::PAGE_SIZE, this->infile);
            if (result == NULL) break;
            line_buffer += std::strlen(line_buffer);
            size_t current_strlen = line_buffer - buffer.get();

            if (current_strlen >= 0.9 * BUFFER_UPPER_LIMIT) {
                processed += (line_buffer - buffer.get());
                std::unique_lock<std::mutex> lock{ this->feed_lock };

                this->feed_buffer.push_back(std::make_pair<>(std::move(buffer), current_strlen));

                buffer = std::unique_ptr<char[]>(new char[BUFFER_UPPER_LIMIT]); // New pointer
                line_buffer = buffer.get();
                line_buffer[0] = '\0';

                this->feed_cond.notify_one();
            }
        }

        // Feed remaining bits
        std::unique_lock<std::mutex> lock{ this->feed_lock };
        this->feed_buffer.push_back(std::make_pair<>(std::move(buffer), line_buffer - buffer.get()));
        this->feed_buffer.push_back(std::make_pair<>(nullptr, 0)); // Termination signal
        this->feed_cond.notify_one();
        lock.unlock();
        worker.join();

        if (std::feof(this->infile)) {
            this->end_feed();
            this->close();
        }
    }

    /** Close the open file handle.
     *
     *  @note Automatically called by ~CSVReader().
     */
    void CSVReader::close() {
        if (this->infile) {
            std::fclose(this->infile);
            this->infile = nullptr;
        }
    }

    /**
     * Retrieve rows as CSVRow objects, returning true if more rows are available.
     *
     * **Performance Notes**:
     *  - The number of rows read in at a time is determined by csv::ITERATION_CHUNK_SIZE
     *  - For performance details, read the documentation for CSVRow and CSVField.
     *
     * @param[out] row The variable where the parsed row will be stored
     * @see CSVRow, CSVField
     *
     * **Example:**
     * \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
    bool CSVReader::read_row(CSVRow &row) {
        if (this->records.empty()) {
            if (!this->eof()) {
                // TODO/Suggestion: Make this call non-blocking, 
                // i.e. move to it another thread
                this->read_csv(internals::ITERATION_CHUNK_SIZE);
            }
            else return false; // Stop reading
        }

        row = std::move(this->records.front());
        this->records.pop_front();

        return true;
    }
}
