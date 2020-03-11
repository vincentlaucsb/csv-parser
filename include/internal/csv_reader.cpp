/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#include <algorithm>
#include <cstdio>   // For read_csv()
#include <cstring>  // For read_csv()
#include <fstream>
#include <sstream>

#include "constants.hpp"
#include "csv_reader.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE std::string format_row(const std::vector<std::string>& row, csv::string_view delim) {
            /** Print a CSV row */
            std::stringstream ret;
            for (size_t i = 0; i < row.size(); i++) {
                ret << row[i];
                if (i + 1 < row.size()) ret << delim;
                else ret << std::endl;
            }

            return ret.str();
        }
    }

    /** Guess the delimiter used by a delimiter-separated values file */
    CSV_INLINE CSVGuessResult guess_format(csv::string_view filename, const std::vector<char>& delims) {
        auto head = internals::get_csv_head(filename);

        /** For each delimiter, find out which row length was most common.
             *  The delimiter with the longest mode row length wins.
             *  Then, the line number of the header row is the first row with
             *  the mode row length.
             */

        CSVFormat format;
        size_t max_rlen = 0,
               header = 0;
        char current_delim = delims[0];

        for (char cand_delim : delims) {
            // Frequency counter of row length
            std::unordered_map<size_t, size_t> row_tally = { { 0, 0 } };

            // Map row lengths to row num where they first occurred
            std::unordered_map<size_t, size_t> row_when = { { 0, 0 } };

            format.delimiter(cand_delim);

            // Parse the CSV
            auto buffer_ptr = internals::BufferPtr(new internals::RawRowBuffer());
            std::deque<CSVRow> rows;

            auto write_row = [&buffer_ptr, &rows]() {
                rows.push_back(CSVRow(buffer_ptr));
            };

            internals::parse({
                head,
                internals::make_parse_flags(cand_delim, '"'),
                internals::make_ws_flags({}, 0),
                buffer_ptr,
                false,
                write_row
                });

            for (size_t i = 0; i < rows.size(); i++) {
                auto& row = rows[i];

                // Ignore zero-length rows
                if (row.size() > 0) {
                    if (row_tally.find(row.size()) != row_tally.end()) {
                        row_tally[row.size()]++;
                    }
                    else {
                        row_tally[row.size()] = 1;
                        row_when[row.size()] = i;
                    }
                }
            }

            // Most common row length
            auto max = std::max_element(row_tally.begin(), row_tally.end(),
                [](const std::pair<size_t, size_t>& x,
                    const std::pair<size_t, size_t>& y) {
                return x.second < y.second; });

            if (max->first > max_rlen) {
                max_rlen = max->first;
                current_delim = cand_delim;
                header = row_when[max_rlen];
            }
        }

        return { current_delim, (int)header };
    }

    CSV_INLINE void CSVReader::bad_row_handler(std::vector<std::string> record) {
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
    CSV_INLINE CSVReader::CSVReader(CSVFormat format) :
        delimiter(format.get_delim()), quote_char(format.quote_char),
        header_row(format.header), strict(format.strict),
        unicode_bom_scan(!format.unicode_detect) {
        if (!format.col_names.empty()) {
            this->set_col_names(format.col_names);
        }
        
        parse_flags = internals::make_parse_flags(this->delimiter, this->quote_char);
        ws_flags = internals::make_ws_flags(format.trim_chars.data(), format.trim_chars.size());
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
    CSV_INLINE CSVReader::CSVReader(csv::string_view filename, CSVFormat format) {
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
        parse_flags = internals::make_parse_flags(delimiter, quote_char);
        ws_flags = internals::make_ws_flags(format.trim_chars.data(), format.trim_chars.size());

        // Read first 500KB of CSV
        this->fopen(filename);
        this->read_csv(500000);
    }

    /** Return the format of the original raw CSV */
    CSV_INLINE CSVFormat CSVReader::get_format() const {
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
    CSV_INLINE std::vector<std::string> CSVReader::get_col_names() const {
        if (this->col_names) {
            return this->col_names->get_col_names();
        }

        return std::vector<std::string>();
    }

    /** Return the index of the column name if found or
     *         csv::CSV_NOT_FOUND otherwise.
     */
    CSV_INLINE int CSVReader::index_of(csv::string_view col_name) const {
        auto _col_names = this->get_col_names();
        for (size_t i = 0; i < _col_names.size(); i++)
            if (_col_names[i] == col_name) return (int)i;

        return CSV_NOT_FOUND;
    }

    CSV_INLINE void CSVReader::feed(WorkItem&& buff) {
        this->feed( csv::string_view(buff.first.get(), buff.second) );
    }

    CSV_INLINE void CSVReader::feed(csv::string_view in) {
        /** Parse a CSV-formatted string.
         *
         *  @par Usage
         *  Incomplete CSV fragments can be joined together by calling feed() on them sequentially.
         *  
         *  @note
         *  `end_feed()` should be called after the last string.
         */
        this->handle_unicode_bom(in);

        try {
            this->record_buffer = internals::parse({
                in,
                this->parse_flags,
                this->ws_flags,
                this->record_buffer,
                this->strict,
                std::bind(&CSVReader::write_record, this)
                });
        }
        catch (std::runtime_error& err) {
            throw std::runtime_error("Unescaped single quote around line ");
            
            /** TODO: Add this back in+
                std::to_string(this->correct_rows) + " near:\n" +
                std::string(in.substr(i, 100)));
                */
        }
    }

    CSV_INLINE void CSVReader::end_feed() {
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

    CSV_INLINE void CSVReader::write_record() {
        /** Push the current row into a queue if it is the right length.
         *  Drop it otherwise.
         */

        if (this->col_names) {
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
                if (row_size > 0) {
                    bad_row_handler(std::vector<std::string>(CSVRow(
                        this->record_buffer)));
                }
            }
        }
        else if (this->row_num == this->header_row) {
            this->set_col_names(std::vector<std::string>(CSVRow(this->record_buffer)));
        }
        else {
            // Ignore rows before header row
            this->record_buffer->get_row();
        }

        this->row_num++;
    }

    CSV_INLINE void CSVReader::read_csv_worker() {
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

    CSV_INLINE void CSVReader::fopen(csv::string_view filename) {
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
    CSV_INLINE void CSVReader::set_col_names(const std::vector<std::string>& names)
    {
        this->col_names = std::make_shared<internals::ColNames>(names);
        this->record_buffer->col_names = this->col_names;
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
    CSV_INLINE void CSVReader::read_csv(const size_t& bytes) {
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
    CSV_INLINE void CSVReader::close() {
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
    CSV_INLINE bool CSVReader::read_row(CSVRow &row) {
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
