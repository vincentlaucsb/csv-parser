/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#include "csv_reader.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE std::string format_row(const std::vector<std::string>& row, csv::string_view delim) {
            /** Print a CSV row */
            std::stringstream ret;
            for (size_t i = 0; i < row.size(); i++) {
                ret << row[i];
                if (i + 1 < row.size()) ret << delim;
                else ret << '\n';
            }
            ret.flush();

            return ret.str();
        }

        /** Return a CSV's column names
         *
         *  @param[in] filename  Path to CSV file
         *  @param[in] format    Format of the CSV file
         *
         */
        CSV_INLINE std::vector<std::string> _get_col_names(csv::string_view head, CSVFormat format) {
            // Parse the CSV
            auto trim_chars = format.get_trim_chars();
            std::stringstream source(head.data());
            RowCollection rows;

            StreamParser<std::stringstream> parser(source, format);
            parser.set_output(rows);
            parser.next();

            return CSVRow(std::move(rows[format.get_header()]));
        }

        CSV_INLINE GuessScore calculate_score(csv::string_view head, CSVFormat format) {
            // Frequency counter of row length
            std::unordered_map<size_t, size_t> row_tally = { { 0, 0 } };

            // Map row lengths to row num where they first occurred
            std::unordered_map<size_t, size_t> row_when = { { 0, 0 } };

            // Parse the CSV
            std::stringstream source(head.data());
            RowCollection rows;

            StreamParser<std::stringstream> parser(source, format);
            parser.set_output(rows);
            parser.next();

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

            double final_score = 0;
            size_t header_row = 0;

            // Final score is equal to the largest
            // row size times rows of that size
            for (auto& pair : row_tally) {
                auto row_size = pair.first;
                auto row_count = pair.second;
                double score = (double)(row_size * row_count);
                if (score > final_score) {
                    final_score = score;
                    header_row = row_when[row_size];
                }
            }

            return {
                final_score,
                header_row
            };
        }

        /** Guess the delimiter used by a delimiter-separated values file */
        CSV_INLINE CSVGuessResult _guess_format(csv::string_view head, const std::vector<char>& delims) {
            /** For each delimiter, find out which row length was most common.
             *  The delimiter with the longest mode row length wins.
             *  Then, the line number of the header row is the first row with
             *  the mode row length.
             */

            CSVFormat format;
            size_t max_score = 0,
                header = 0;
            char current_delim = delims[0];

            for (char cand_delim : delims) {
                auto result = calculate_score(head, format.delimiter(cand_delim));

                if ((size_t)result.score > max_score) {
                    max_score = (size_t)result.score;
                    current_delim = cand_delim;
                    header = result.header;
                }
            }

            return { current_delim, (int)header };
        }
    }

    /** Return a CSV's column names
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     */
    CSV_INLINE std::vector<std::string> get_col_names(csv::string_view filename, CSVFormat format) {
        auto head = internals::get_csv_head(filename);

        /** Guess delimiter and header row */
        if (format.guess_delim()) {
            auto guess_result = guess_format(filename, format.get_possible_delims());
            format.delimiter(guess_result.delim).header_row(guess_result.header_row);
        }

        return internals::_get_col_names(head, format);
    }

    /** Guess the delimiter used by a delimiter-separated values file */
    CSV_INLINE CSVGuessResult guess_format(csv::string_view filename, const std::vector<char>& delims) {
        auto head = internals::get_csv_head(filename);
        return internals::_guess_format(head, delims);
    }

    /** Reads an arbitrarily large CSV file using memory-mapped IO.
     *
     *  **Details:** Reads the first block of a CSV file synchronously to get information
     *               such as column names and delimiting character.
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     *  \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
	CSV_INLINE CSVReader::CSVReader(csv::string_view filename, CSVFormat format) : _format(format) {
        auto head = internals::get_csv_head(filename);
        using Parser = internals::MmapParser;

        /** Guess delimiter and header row */
        if (format.guess_delim()) {
            auto guess_result = internals::_guess_format(head, format.possible_delimiters);
            format.delimiter(guess_result.delim);
            format.header = guess_result.header_row;
            this->_format = format;
        }

        if (!format.col_names.empty())
            this->set_col_names(format.col_names);

        this->parser = std::unique_ptr<Parser>(new Parser(filename, format, this->col_names)); // For C++11
        this->initial_read();
    }

    /** Return the format of the original raw CSV */
    CSV_INLINE CSVFormat CSVReader::get_format() const {
        CSVFormat new_format = this->_format;

        // Since users are normally not allowed to set
        // column names and header row simulatenously,
        // we will set the backing variables directly here
        new_format.col_names = this->col_names->get_col_names();
        new_format.header = this->_format.header;

        return new_format;
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

    CSV_INLINE void CSVReader::trim_header() {
        if (!this->header_trimmed) {
            for (int i = 0; i <= this->_format.header && !this->records->empty(); i++) {
                if (i == this->_format.header && this->col_names->empty()) {
                    this->set_col_names(this->records->pop_front());
                }
                else {
                    this->records->pop_front();
                }
            }

            this->header_trimmed = true;
        }
    }

    /**
     *  @param[in] names Column names
     */
    CSV_INLINE void CSVReader::set_col_names(const std::vector<std::string>& names)
    {
        this->col_names->set_col_names(names);
        this->n_cols = names.size();
    }

    /**
     * Read a chunk of CSV data.
     *
     * @note This method is meant to be run on its own thread. Only one `read_csv()` thread
     *       should be active at a time.
     *
     * @param[in] bytes Number of bytes to read.
     *
     * @see CSVReader::read_csv_worker
     * @see CSVReader::read_row()
     */
    CSV_INLINE bool CSVReader::read_csv(size_t bytes) {
        // Tell read_row() to listen for CSV rows
        this->records->notify_all();

        this->parser->set_output(*this->records);
        this->parser->next(bytes);

        if (!this->header_trimmed) {
            this->trim_header();
        }

        // Tell read_row() to stop waiting
        this->records->kill_all();

        return true;
    }

    /**
     * Retrieve rows as CSVRow objects, returning true if more rows are available.
     *
     * @par Performance Notes
     *  - Reads chunks of data that are csv::internals::ITERATION_CHUNK_SIZE bytes large at a time
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
        while (true) {
            if (this->records->empty()) {
                if (this->records->is_waitable())
                    // Reading thread is currently active => wait for it to populate records
                    this->records->wait();
                else if (this->parser->eof())
                    // End of file and no more records
                    return false;
                else {
                    // Reading thread is not active => start another one
                    if (this->read_csv_worker.joinable())
                        this->read_csv_worker.join();

                    this->read_csv_worker = std::thread(&CSVReader::read_csv, this, internals::ITERATION_CHUNK_SIZE);
                }
            }
            else if (this->records->front().size() != this->n_cols &&
                this->_format.variable_column_policy != VariableColumnPolicy::KEEP) {
                auto errored_row = this->records->pop_front();

                if (this->_format.variable_column_policy == VariableColumnPolicy::THROW) {
                    if (errored_row.size() < this->n_cols)
                        throw std::runtime_error("Line too short " + internals::format_row(errored_row));

                    throw std::runtime_error("Line too long " + internals::format_row(errored_row));
                }
            }
            else {
                row = this->records->pop_front();
                this->_n_rows++;
                return true;
            }
        }

        return false;
    }
}
