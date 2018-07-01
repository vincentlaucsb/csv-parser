#include "csv_parser.hpp"

/** @file
 *  @brief Defines all functionality needed for basic CSV parsing
 */

namespace csv {
    namespace internals {
        bool is_equal(double a, double b, double epsilon) {
            /** Returns true if two doubles are about the same */
            return std::abs(a - b) < epsilon;
        }

        std::string format_row(const std::vector<std::string>& row, const std::string& delim) {
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

        void CSVGuesser::guess_delim() {
            /** Guess the delimiter of a CSV by scanning the first 100 lines by
            *  First assuming that the header is on the first row
            *  If the first guess returns too few rows, then we move to the second
            *  guess method
            */
            if (!first_guess()) second_guess();
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

            CSVFormat format = DEFAULT_CSV;
            char current_delim{ ',' };
            int max_rows = 0;
            int temp_rows = 0;
            size_t max_cols = 0;

            for (size_t i = 0; i < delims.size(); i++) {
                format.delim = delims[i];
                CSVReader guesser(this->filename, format);

                // WORKAROUND on Unix systems because certain newlines
                // get double counted
                // temp_rows = guesser.correct_rows;
                temp_rows = std::min(guesser.correct_rows, (RowCount)100);
                if ((guesser.row_num >= max_rows) &&
                    (guesser.get_col_names().size() > max_cols)) {
                    max_rows = temp_rows;
                    max_cols = guesser.get_col_names().size();
                    current_delim = delims[i];
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

            CSVFormat format = DEFAULT_CSV;
            size_t max_rlen = 0;
            size_t header = 0;

            for (auto it = delims.begin(); it != delims.end(); ++it) {
                format.delim = *it;
                Guesser guess(format);
                guess.read_csv(filename, 100);

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
    }

    /** @brief Guess the delimiter used by a delimiter-separated values file */
    CSVFormat guess_format(const std::string& filename) {
        internals::CSVGuesser guesser(filename);
        guesser.guess_delim();
        return { guesser.delim, '"', guesser.header_row };
    }

    std::vector<CSVReader::ParseFlags> CSVReader::make_flags() const {
        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */

        std::vector<ParseFlags> ret;
        for (int i = -128; i < 128; i++) {
            char ch = char(i);

            if (ch == this->delimiter)
                ret.push_back(DELIMITER);
            else if (ch == this->quote_char)
                ret.push_back(QUOTE);
            else if (ch == '\r' || ch == '\n')
                ret.push_back(NEWLINE);
            else
                ret.push_back(NOT_SPECIAL);
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

    /**
     *  @brief Shorthand function for parsing an in-memory CSV string,
     *  a collection of CSVRow objects
     *
     *  \snippet tests/test_read_csv.cpp Parse Example
     *
     */
    CSVCollection parse(const std::string& in, CSVFormat format) {
        CSVReader parser(format);
        parser.feed(in);
        parser.end_feed();
        return parser.records;
    }

    /** 
     * @brief Parse a RFC 4180 CSV string, returning a collection
     *        of CSVRow objects
     *
     * **Example:**
     *  \snippet tests/test_read_csv.cpp Escaped Comma
     *
     */
    CSVCollection operator ""_csv(const char* in, size_t n) {    
        std::string temp(in, n);
        return parse(temp);
    }

    /**
     *  @brief Return a CSV's column names
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     */
    std::vector<std::string> get_col_names(const std::string& filename, CSVFormat format) {
        CSVReader reader(filename, format);
        return reader.get_col_names();
    }

    int get_col_pos(
        const std::string filename,
        const std::string col_name,
        const CSVFormat format) {
        /** Find the position of a column in a CSV file or CSV_NOT_FOUND otherwise
        *  @param[in] filename  Path to CSV file
        *  @param[in] col_name  Column whose position we should resolve
        *  @param[in] format    Format of the CSV file
        */

        CSVReader reader(filename, format);
        return reader.index_of(col_name);
    }

    /** @brief Get basic information about a CSV file
     *  \include programs/csv_info.cpp
     */
    CSVFileInfo get_file_info(const std::string& filename) {
        CSVReader reader(filename);
        CSVFormat format = reader.get_format();
        CSVRow row;
        while (reader.read_row(row));

        CSVFileInfo info = {
            filename,
            reader.get_col_names(),
            format.delim,
            reader.correct_rows,
            (int)reader.get_col_names().size()
        };

        return info;
    }

    /**
     *  @brief Allows parsing in-memory sources (by calling feed() and end_feed()).
     */
    CSVReader::CSVReader(CSVFormat format) :
        delimiter(format.delim), quote_char(format.quote_char),
        header_row(format.header), strict(format.strict) {
        if (!format.col_names.empty()) {
            this->header_row = -1;
            this->col_names = std::make_shared<internals::ColNames>(format.col_names);
        }
    };

    /**
     *  @brief Allows reading a CSV file in chunks, using overlapped
     *          threads for simulatenously reading from disk and parsing.
     *          Rows should be retrieved with read_row().

     *  @detail Reads the first 100 rows of a CSV file to infer file information
     *          such as column names and delimiting character.
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     *  \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
    CSVReader::CSVReader(const std::string& filename, CSVFormat format) {
        if (format.delim == '\0')
            format = guess_format(filename);

        this->col_names = std::make_shared<internals::ColNames>(format.col_names);
        delimiter = format.delim;
        quote_char = format.quote_char;
        header_row = format.header;
        strict = format.strict;

        // Begin reading CSV
        read_csv(filename, 100, false);
    }

    /** @brief Return the format of the original raw CSV */
    CSVFormat CSVReader::get_format() const {
        return {
            this->delimiter,
            this->quote_char,
            this->header_row,
            this->col_names->col_names
        };
    }

    /** @brief Return the CSV's column names as a vector of strings. */
    std::vector<std::string> CSVReader::get_col_names() const {
        return this->col_names->get_col_names();
    }

    /** @brief Return the index of the column name if found or
     *         csv::CSV_NOT_FOUND otherwise.
     */
    int CSVReader::index_of(const std::string& col_name) const {
        auto col_names = this->get_col_names();
        for (size_t i = 0; i < col_names.size(); i++)
            if (col_names[i] == col_name) return (int)i;

        return CSV_NOT_FOUND;
    }

    void CSVReader::feed(std::unique_ptr<std::string>&& buff) {
        this->feed(std::string_view(buff->c_str()));
    }

    void CSVReader::feed(std::string_view in) {
        /** Parse a CSV-formatted string. Incomplete CSV fragments can be void print_row(const std::vector<std::string>& row);name
         *  joined together by calling feed() on them sequentially.
         *  **Note**: end_feed() should be called after the last string
         */

        if (parse_flags.empty()) parse_flags = this->make_flags();

        for (this->c_pos = 0; c_pos < in.size(); c_pos++) {
            const char& ch = in[c_pos];
            switch (this->parse_flags[ch + 128]) {
            case NOT_SPECIAL:
                this->record_buffer += ch;
                this->n_pos++;
                break;
            case DELIMITER:
                this->process_possible_delim(in);
                break;
            case NEWLINE:
                this->process_newline(in);
                break;
            default: // Quote
                this->process_quote(in);
                break;
            }
        }
    }

    void CSVReader::end_feed() {
        /** Indicate that there is no more data to receive,
        *  and handle the last row
        */
        this->write_record();
    }

    void CSVReader::process_possible_delim(std::string_view sv) {
        /** Process a delimiter character and determine if it is a field
        *  separator
        */

        if (!this->quote_escape) // Make a new field
            this->split_buffer.push_back(this->n_pos);
        else { // Treat as a regular character
            this->record_buffer += sv[c_pos];
            this->n_pos++;
        }
    }

    void CSVReader::process_newline(std::string_view sv) {
        /** Process a newline character and determine if it is a record
        *  separator
        */
        if (!this->quote_escape) {
            // Case: Carriage Return Line Feed, Carriage Return, or Line Feed
            // => End of record -> Write record
            if ((sv[c_pos] == '\r') && (sv[c_pos + 1] == '\n'))
                ++c_pos;
            this->write_record();
        }
        else { // Treat as a regular character
            this->record_buffer += sv[c_pos];
            this->n_pos++;
        }
    }

    void CSVReader::process_quote(std::string_view sv) {
        /** Determine if the usage of a quote is valid or fix it */
        if (this->quote_escape) {
            auto next_ch = this->parse_flags[sv[c_pos + 1] + 128];
            if (next_ch == DELIMITER || next_ch == NEWLINE) {
                // Case: End of field
                this->quote_escape = false;
            }
            else {
                this->record_buffer += sv[c_pos];
                this->n_pos++;

                if (next_ch == QUOTE)
                    ++c_pos;  // Case: Two consecutive quotes
                else if (this->strict)
                    throw std::runtime_error("Unescaped single quote around line " +
                        std::to_string(this->correct_rows) + " near:\n" +
                        std::string(sv.substr(c_pos, 100)));
            }
        }
        else {
            // Case: Previous character was delimiter
            // Don't deref past beginning
            if (c_pos) {
                auto prev_ch = this->parse_flags[sv[c_pos - 1] + 128];
                if (prev_ch == DELIMITER || prev_ch == NEWLINE)
                    this->quote_escape = true;
            }
        }
    }

    void CSVReader::write_record() {
        /** Push the current row into a queue if it is the right length.
         *  Drop it otherwise.
         */

        size_t col_names_size = this->col_names->size();
        this->min_row_len = std::min(this->min_row_len, this->record_buffer.size());
        this->n_pos = 0;
        this->quote_escape = false;  // Unset all flags
        auto row = CSVRow(
            std::move(this->record_buffer),
            std::move(this->split_buffer),
            this->col_names
        );

        if (this->row_num > this->header_row) {
            // Make sure record is of the right length
            if (row.size() == col_names_size) {
                this->correct_rows++;
                this->records.push_back(row);
            }
            else {
                /* 1) Zero-length record, probably caused by extraneous newlines
                 * 2) Too short or too long
                 */
                this->row_num--;
                if (!row.empty())
                    bad_row_handler(std::vector<std::string>(row));
            }
        }
        else if (this->row_num == this->header_row) {
            this->col_names = std::make_shared<internals::ColNames>(
                std::vector<std::string>(row));
        } // else: Ignore rows before header row

        this->record_buffer = "";
        if (this->record_buffer.capacity() < this->min_row_len * 1.5)
            record_buffer.reserve(this->min_row_len * 1.5);

        this->split_buffer = {};
        if (this->split_buffer.capacity() < col_names_size)
            split_buffer.reserve(col_names_size);

        this->row_num++;
    }

    void CSVReader::read_csv_worker() {
        /** @brief Worker thread for read_csv() which parses CSV rows (while the main
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
            if (!in) break;

            lock.unlock();      // Release lock
            this->feed(std::move(in));
        }
    }

    /**
     * @brief Parse a CSV file using multiple threads
     *
     * @param[in] nrows Number of rows to read. Set to -1 to read entire file.
     * @param[in] close Close file after reading?
     *
     * @see CSVReader::read_row()
     * 
     */
    void CSVReader::read_csv(std::string filename, int nrows, bool close) {

        if (!this->infile) {
            this->infile = std::fopen(filename.c_str(), "r");
            if (!this->infile)
                throw std::runtime_error("Cannot open file " + filename);
        }

        std::unique_ptr<char[]> line_buffer(new char[PAGE_SIZE]);
        auto buffer = std::make_unique<std::string>();
        std::thread worker(&CSVReader::read_csv_worker, this);

        while (nrows <= -1 || nrows > 0) {
            char * result = std::fgets(line_buffer.get(), PAGE_SIZE, this->infile);
            if (result == NULL) break;
            else {
                *buffer += line_buffer.get();
                if (std::feof(this->infile)) break;
            }

            nrows--;

            if ((*buffer).size() >= 1000000) {
                std::unique_lock<std::mutex> lock{ this->feed_lock };
                this->feed_buffer.push_back(std::move(buffer));
                this->feed_cond.notify_one();
                buffer = std::make_unique<std::string>(); // New pointer
            }
        }

        // Feed remaining bits
        std::unique_lock<std::mutex> lock{ this->feed_lock };
        this->feed_buffer.push_back(std::move(buffer));
        this->feed_buffer.push_back(nullptr); // Termination signal
        this->feed_cond.notify_one();
        lock.unlock();
        worker.join();

        if (std::feof(this->infile)) {
            this->end_feed();
            this->close();
        }
    }

    void CSVReader::close() {
        if (this->infile) {
            std::fclose(this->infile);
            this->infile = nullptr;
        }
    }

    /**
     * @brief Retrieve rows as CSVRow objects, returning true if more rows are available.
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
                this->read_csv("", ITERATION_CHUNK_SIZE, false);
            }
            else return false; // Stop reading
        }

        row = std::move(this->records.front());
        this->records.pop_front();

        return true;
    }
}