/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#include <algorithm>
#include <future>
#include <cstring>  // For read_csv()
#include <fstream>
#include <sstream>
#include <iostream>
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

        /** Return a CSV's column names
         *
         *  @param[in] filename  Path to CSV file
         *  @param[in] format    Format of the CSV file
         *
         */
        CSV_INLINE std::vector<std::string> _get_col_names(csv::string_view head, CSVFormat format) {
            auto parse_flags = internals::make_parse_flags(format.get_delim());
            if (format.is_quoting_enabled()) {
                parse_flags = internals::make_parse_flags(format.get_delim(), format.get_quote_char());
            }

            // Parse the CSV
            auto trim_chars = format.get_trim_chars();

            BasicCSVParser parser(
                parse_flags,
                internals::make_ws_flags(trim_chars.data(), trim_chars.size())
            );

            ThreadSafeDeque<CSVRow> rows;
            parser.parse(head, rows);

            return CSVRow(std::move(rows[format.get_header()]));
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

    /** Allows parsing in-memory sources (by calling feed() and end_feed()). */
    CSV_INLINE CSVReader::CSVReader(CSVFormat format) : 
        unicode_bom_scan(!format.unicode_detect) {
        if (!format.col_names.empty()) {
            this->set_col_names(format.col_names);
        }
        
        this->set_parse_flags(format);
    }

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
        this->_filename = filename;
        this->mmap_eof = false;
        std::ifstream infile(std::string(filename), std::ios::binary);
        const auto start = infile.tellg();
        infile.seekg(0, std::ios::end);
        const auto end = infile.tellg();
        this->file_size = end - start;

        auto head = internals::get_csv_head(filename, this->file_size);

        /** Guess delimiter and header row */
        if (format.guess_delim()) {
            auto guess_result = internals::_guess_format(head, format.possible_delimiters);
            format.delimiter(guess_result.delim);
            format.header = guess_result.header_row;
        }

        if (format.col_names.empty()) {
            this->set_col_names(internals::_get_col_names(head, format));
        }
        else {
            this->set_col_names(format.col_names);
        }

        this->set_parse_flags(format);
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

    CSV_INLINE size_t CSVReader::feed_map(mio::mmap_source&& source, bool last_block) {
        this->parser.data_source = std::move(source);

        return this->feed(
            csv::string_view(this->parser.data_source.data(), this->parser.data_source.length()),
            last_block
        );
    }

    /** Parse a CSV-formatted string.
     *
     *  @par Usage
     *  Incomplete CSV fragments can be joined together by calling feed() on them sequentially.
     *
     *  @note
     *  `end_feed()` should be called after the last string.
     */
    CSV_INLINE size_t CSVReader::feed(csv::string_view in, bool last_block) {
        if (in.empty()) return 0;

        /** Handle possible Unicode byte order mark */
        if (!this->unicode_bom_scan) {
            if (in[0] == '\xEF' && in[1] == '\xBB' && in[2] == '\xBF') {
                in.remove_prefix(3); // Remove BOM from input string
                this->_utf8_bom = true;
            }

            this->unicode_bom_scan = true;
        }

        size_t offset = this->parser.parse(in, this->records, last_block);

        if (!this->header_trimmed) {
            for (int i = 0; i <= this->_format.header && !this->records.empty(); i++) {
                if (i == this->_format.header && this->col_names->empty())
                    this->set_col_names(this->records.pop_front());
                else
                    this->records.pop_front();
            }

            this->header_trimmed = true;
        }

        return offset;
    }

    CSV_INLINE void CSVReader::set_parse_flags(const CSVFormat& format)
    {
        this->_format = format;
        if (format.no_quote) {
            this->parser.set_parse_flags(internals::make_parse_flags(format.get_delim()));
        }
        else {
            this->parser.set_parse_flags(internals::make_parse_flags(format.get_delim(), format.quote_char));
        }

        this->parser.set_ws_flags(internals::make_ws_flags(format.trim_chars.data(), format.trim_chars.size()));
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
     * Parse a CSV file.
     *
     * @param[in] bytes Number of bytes to read.
     * @see CSVReader::read_row()
     */
    CSV_INLINE bool CSVReader::read_csv(const size_t& bytes) {
        if (this->_filename.empty()) {
            return false;
        }

        std::error_code error;

        size_t length = csv::internals::ITERATION_CHUNK_SIZE;
        size_t remainder = this->file_size - this->mmap_pos;
        bool last_block = false;

        if (remainder < length) {
            length = remainder;
            last_block = true;
        }

        auto csv_mmap = mio::make_mmap_source(this->_filename, this->mmap_pos, length, error);
        this->mmap_pos += length;

        if (error) {
            throw error;
        }

        this->records.start_waiters();

        size_t offset = this->feed_map(std::move(csv_mmap), last_block);
        this->mmap_pos -= offset;

        if (last_block) {
            this->mmap_eof = true;
        }

        this->records.stop_waiters();

        return true;
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
        while (true) {
            if (this->records.empty()) {
                if (!this->records.stop_waiting)
                    this->records.wait_for_data();
                else {
                    if (this->read_rows_worker.joinable())
                        this->read_rows_worker.join();

                    if (this->eof()) return false;

                    this->read_rows_worker = std::thread(
                        &CSVReader::read_csv, this, internals::ITERATION_CHUNK_SIZE);
                }
            }
            else {
                if (this->records.front().size() != this->n_cols &&
                    this->_format.variable_column_policy != VariableColumnPolicy::KEEP) {
                    auto errored_row = this->records.pop_front();

                    if (this->_format.variable_column_policy == VariableColumnPolicy::THROW) {
                        if (errored_row.size() < this->n_cols) {
                            throw std::runtime_error("Line too short " + internals::format_row(errored_row));
                        }

                        throw std::runtime_error("Line too long " + internals::format_row(errored_row));
                    }

                    // Silently do nothing
                    continue;
                }
                
                row = std::move(this->records.pop_front());
                this->n_rows++;
                return true;
            }
        }
    
        return false;
    }
}