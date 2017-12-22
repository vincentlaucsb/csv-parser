#include "csv_parser.h"
#include <random>

namespace csv {
    /** @file
     *  Defines all functionality needed for basic CSV parsing
     */

    char guess_delim(const std::string filename) {
        /** Guess the delimiter of a delimiter separated values file
         *  by scanning the first 100 lines
         *
         *  - "Winner" is based on which delimiter has the most number
         *    of correctly parsed rows + largest number of columns
         *  -  **Note:** Assumes that whatever the dialect, all records
         *     are newline separated
         */

        std::vector<char> delims = { ',', '|', '\t', ';', '^' };
        char current_delim;
        int max_rows = 0;
        size_t max_cols = 0;

        for (size_t i = 0; i < delims.size(); i++) {
            CSVReader guesser(delims[i]);
            guesser.read_csv(filename, 100);
            if ((guesser.row_num >= max_rows) &&
                (guesser.get_col_names().size() > max_cols)) {
                max_rows = guesser.row_num > max_rows;
                max_cols = guesser.get_col_names().size();
                current_delim = delims[i];
            }
        }

        return current_delim;
    }
    
    std::vector<std::string> get_col_names(const std::string filename, CSVFormat format) {
        /** Return a CSV's column names
         *  @param[in] filename  Path to CSV file
         *  @param[in] format    Format of the CSV file
         */
        CSVReader reader(filename, format);
        reader.close();
        return reader.get_col_names();
    }

    int get_col_pos(
        const std::string filename,
        const std::string col_name,
        const CSVFormat format) {
        /** Find the position of a column in a CSV file
         *  @param[in] filename  Path to CSV file
         *  @param[in] col_name  Column whose position we should resolve
         *  @param[in] format    Format of the CSV file
         */
        std::vector<std::string> col_names = get_col_names(filename, format);

        auto it = std::find(col_names.begin(), col_names.end(), col_name);
        if (it != col_names.end())
            return it - col_names.begin();
        else
            return -1;
    }

    CSVFileInfo get_file_info(const std::string filename) {
        /** Get basic information about a CSV file */
        CSVReader reader(filename);
        CSVFormat format = reader.get_format();
        std::vector<std::string> row;

        /* Loop over entire file without doing anything
         * except for counting up the number of rows
         */
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

    CSVReader::CSVReader(std::string filename, CSVFormat format, std::vector<int> _subset) {
        /** Create a CSVReader over a file. This constructor
         *  first reads the first 100 rows of a CSV file. After that, you can
         *  lazily iterate over a file by repeatedly calling any one of the 
         *  CSVReader::read_row() functions
         *
         *  @param[in] filename  Path to CSV file
         *  @param[in] format    Format of the CSV file
         *  @param[in] subset    Indices of columns to keep (default: keep all)
         */
        if (format.delim == '\0')
            delimiter = guess_delim(filename);
        else
            delimiter = format.delim;

        quote_char = format.quote_char;
        header_row = format.header;
        subset = _subset;

        // Begin reading CSV
        read_csv(filename, 100, false);
        read_start = true;
        current_row = records.begin();
    }

    CSVFormat CSVReader::get_format() {
        /** Return the format of the original raw CSV */
        return {
            this->delimiter,
            this->quote_char,
            this->header_row
        };
    }

    void CSVReader::set_col_names(std::vector<std::string> col_names) {
        /** Set or override the CSV's column names
         * 
         *  #### Significance
         *  - When parsing, rows that are shorter or longer than the list 
         *    of column names get dropped
         *  - These column names are also used when creating CSV/JSON/SQLite3 files
         *
         *  @param[in] col_names Column names
         */
        
        this->col_names = col_names;
        
        if (this->subset.size() > 0) {
            this->subset_flag = true;
            for (size_t i = 0; i < this->subset.size(); i++) {
                subset_col_names.push_back(col_names[this->subset[i]]);
            }
        } else {
            // "Subset" is every column
            for (size_t i = 0; i < this->col_names.size(); i++) {
                this->subset.push_back(i);
            }
            subset_col_names = col_names;
        }
    }

    std::vector<std::string> CSVReader::get_col_names() {
        return this->subset_col_names;
    }

    void CSVReader::feed(const std::string &in) {
        /** Parse a CSV-formatted string. Incomplete CSV fragments can be 
         *  joined together by calling feed() on them sequentially.
         *  **Note**: end_feed() should be called after the last string
         */

        std::string * str_buffer = &(this->record_buffer.back());

        for (size_t i = 0, ilen = in.length(); i < ilen; i++) {
            if (in[i] == this->delimiter) {
                this->process_possible_delim(in, i, str_buffer);
            } else if (in[i] == this->quote_char) {
                this->process_quote(in, i, str_buffer);
            } else {
                switch(in[i]) {
                    case '\r':
                    case '\n':
                        this->process_newline(in, i, str_buffer);
                        break;
                    default:
                        *str_buffer += in[i];
                }
            }
        }
    }

    void CSVReader::end_feed() {
        /** Indicate that there is no more data to receive,
         *  and handle the last row
         */
        this->write_record(this->record_buffer);
    }

    void CSVReader::process_possible_delim(
        const std::string &in, size_t &index, std::string* &out) {
        /** Process a delimiter character and determine if it is a field
         *  separator
         */

        if (!this->quote_escape) {
            // Case: Not being escaped --> Write field
            this->record_buffer.push_back(std::string());
            out = &(this->record_buffer.back());
        } else {
            *out += in[index]; // Treat as regular data
        }
    }

    void CSVReader::process_newline(
        const std::string &in, size_t &index, std::string* &out) {
        /** Process a newline character and determine if it is a record
         *  separator        
         */
        if (!this->quote_escape) {
            // Case: Carriage Return Line Feed, Carriage Return, or Line Feed
            // => End of record -> Write record
            if ((in[index] == '\r') && (in[index + 1] == '\n')) {
                index++;
            }

            this->write_record(this->record_buffer);
            out = &(this->record_buffer.back());
        } else {
            *out += in[index]; // Quote-escaped
        }
    }

    void CSVReader::process_quote(
        const std::string &in, size_t &index, std::string* &out) {
        /** Determine if the usage of a quote is valid or fix it
         */
        if (this->quote_escape) {
            if ((in[index + 1] == this->delimiter) || 
                (in[index + 1] == '\r') ||
                (in[index + 1] == '\n')) {
                // Case: End of field
                this->quote_escape = false;
            } else {
                // Note: This may fix single quotes (not strictly valid)
                *out += in[index];
                if (in[index + 1] == this->quote_char)
                    index++;  // Case: Two consecutive quotes
            }
        } else {
			/* Add index > 0 to prevent string index errors
             * Case: Previous character was delimiter
             */
            if (index > 0 && in[index - 1] == this->delimiter)
                this->quote_escape = true;
        }
    }

    void CSVReader::write_record(std::vector<std::string>& record) {
        /** Push the current row into a queue if it is the right length.
         *  Drop it otherwise.
         */
        
        size_t col_names_size = this->col_names.size();
        this->quote_escape = false;  // Unset all flags
        
        if (this->row_num > this->header_row) {
            // Make sure record is of the right length
            if (record.size() == col_names_size) {
                this->correct_rows++;

                if (!this->subset_flag) {
                    this->records.push_back({});
                    record.swap(this->records.back());
                }
                else {
                    std::vector<std::string> subset_record;
                    for (size_t i = 0; i < this->subset.size(); i++)
                        subset_record.push_back(record[this->subset[i] ]);
                    this->records.push_back(subset_record);
                }
            } else {
                /* 1) Zero-length record, probably caused by extraneous newlines
                 * 2) Too short or too long
                 */
                this->row_num--;
                if (!record.empty() && this->bad_row_handler) {
                    auto copy = record;
                    this->bad_row_handler(copy);
                }
            }
        } else if (this->row_num == this->header_row) {
            this->set_col_names(record);
        } else {
            // Ignore rows before header row
        }

        record.clear();
        record.push_back(std::string());
        this->row_num++;
    }

    bool CSVReader::empty() {
        /** Indicates whether or not the queue still contains CSV rows */
        return this->records.empty();
    }

    void CSVReader::clear() {
        this->records.clear();
    }

    void CSVReader::_read_csv() {
        /** Multi-threaded reading/processing worker thread */

        while (true) {
            std::unique_lock<std::mutex> lock{ this->feed_lock }; // Get lock
            this->feed_cond.wait(lock,                            // Wait
                [this] { return !(this->feed_buffer.empty()); });

            // Wake-up
            std::string* in = this->feed_buffer.front();
            this->feed_buffer.pop_front();

            if (!in) { // Nullptr --> Die
                delete in;
                break;    
            }

            lock.unlock();      // Release lock
            this->feed(*in);
            delete in;          // Free buffer
        }
    }

    void CSVReader::read_csv(std::string filename, int nrows, bool close) {
        /** Parse an entire CSV file */

        if (!this->infile) {
            this->infile = std::fopen(filename.c_str(), "r");
            this->infile_name = filename;

            if (!this->infile)
                throw std::runtime_error("Cannot open file " + filename);
        }

        char * line_buffer = new char[10000];
        std::string* buffer = new std::string();
        std::thread worker(&CSVReader::_read_csv, this);

        while (nrows <= -1 || nrows > 0) {
            char * result = std::fgets(line_buffer, sizeof(char[10000]), this->infile);            
            if (result == NULL || std::feof(this->infile)) {
                break;
            }
            else {
                *buffer += line_buffer;
            }

            nrows--;

            if ((*buffer).size() >= 1000000) {
                std::unique_lock<std::mutex> lock{ this->feed_lock };
                this->feed_buffer.push_back(buffer);
                this->feed_cond.notify_one();
                buffer = new std::string();  // Buffers get freed by worker
                // Doesn't help oddly enough: buffer->reserve(1000000);
            }
        }

        // Feed remaining bits
        std::unique_lock<std::mutex> lock{ this->feed_lock };
        this->feed_buffer.push_back(buffer);
        this->feed_buffer.push_back(nullptr); // Termination signal
        this->feed_cond.notify_one();
        delete[] line_buffer;
        lock.unlock();
        worker.join();

        if (std::feof(this->infile) || (close && !this->eof)) {
            this->end_feed();
            this->eof = true;
            this->close();
        }
    }

    void CSVReader::close() {
        std::fclose(this->infile);
    }
    
    std::string CSVReader::csv_to_json(std::vector<std::string>& record) {
        /** Helper method for both to_json() methods */
        std::string json_record = "{";
        std::string * col_name;
        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            col_name = &this->subset_col_names[i];
            json_record += "\"" + *col_name + "\":";
            
            /* Quote strings but not numeric fields
             * Recall data_type() returns 2 for ints and 3 for floats
             */
            
            if (helpers::data_type(record[i]) > 1)
                json_record += record[i];
            else
                json_record += "\"" + helpers::json_escape(record[i]) + "\"";

            if (i + 1 != record.size())
                json_record += ",";
        }

        json_record += "}";
        return json_record;
    }

    void CSVReader::to_json(std::string filename, bool append) {
        /** Convert CSV to a newline-delimited JSON file, where each
         *  row is mapped to an object with the column names as keys.
         *
         *  # Example
         *  ## Input
         *  <TABLE>
         *      <TR><TH>Name</TH><TH>TD</TH><TH>Int</TH><TH>Yards</TH></TR>
         *      <TR><TD>Tom Brady</TD><TD>2</TD><TD>1</TD><TD>466</TD></TR>
         *      <TR><TD>Matt Ryan</TD><TD>2</TD><TD>0</TD><TD>284</TD></TR>
         *  </TABLE>
         *
         *  ## Output
         *  > to_json("twentyeight-three.ndjson")
         *
         *  > {"Name":"Tom Brady","TD":2,"Int":1,"Yards":466}
         *  >
         *  > {"Name":"Matt Ryan","TD":2,"Int":0,"Yards":284}
         */
        std::vector<std::string> record;
        std::ofstream outfile;

        if (append)
            outfile.open(filename, std::ios_base::app);
        else
            outfile.open(filename);

        for (auto it = this->records.begin(); it != this->records.end(); ++it)
            outfile << this->csv_to_json(*it) + "\n";

        outfile.close();
    }

    std::vector<std::string> CSVReader::to_json() {
        /** Similar to to_json(std::string filename), but outputs a vector of
         *  JSON strings instead
         */
        std::vector<std::string> output;

        for (auto it = this->records.begin(); it != this->records.end(); ++it)
            output.push_back(this->csv_to_json(*it));

        return output;
    }

    /*
    void CSVReader::sample(int n) {
         Take a random uniform sample (with replacement) of n rows
        std::deque<std::vector<std::string>> new_rows;
        std::default_random_engine generator;
        std::uniform_int_distribution<int> distribution(0, this->records.size() - 1);

        for (; n > 1; n--)
            new_rows.push_back(this->records[distribution(generator)]);

        this->clear();
        this->records.swap(new_rows);
    }
    */

    bool CSVReader::read_row(std::vector<std::string> &row) {
        /** Retrieve rows parsed by CSVReader in FIFO order.
         *   - If CSVReader was initialized with respect to a file, then this lazily
         *     iterates over the file until no more rows are available.
         *
         *  #### Return Value
         *  Returns True if more rows are available, False otherwise.
         *
         *  #### Alternatives 
         *  If you want automatic type casting of values, then consider
         *   - CSVReader::read_row(std::vector<CSVField> &row) or
         *   - CSVReader::read_row(std::vector<void *>&, std::vector<int>&, bool*)
         *
         *  @param[out] row A vector of strings where the read row will be stored
         */
        while (true) {
            if (!read_start) {
                this->read_start = true;
                this->current_row = this->records.begin();
            }

            if (this->current_row == this->records.end()) {
                this->clear();

                if (!this->eof && this->infile) {
                    this->read_csv("", ITERATION_CHUNK_SIZE, false);
                    this->current_row = this->records.begin();
                }
                else {
                    break;
                }
            }

            row.swap(*(this->current_row));
            this->current_row++;
            return true;
        }

        return false;
    }

    bool CSVReader::read_row(std::vector<CSVField> &row) {
        /** Perform automatic type-casting when retrieving rows
         *  - Much faster and more robust than calling std::stoi()
         *    on the values of a std::vector<std::string>
         *
         *  **Note:** See the documentation for CSVField to see how to work
         *  with the output of this function
         *
         *  @param[out] row A vector of strings where the read row will be stored
         */
        while (true) {
            if (!read_start) {
                this->read_start = true;
                this->current_row = this->records.begin();
            }

            if (this->current_row == this->records.end()) {
                this->clear();

                if (!this->eof && this->infile) {
                    this->read_csv("", ITERATION_CHUNK_SIZE, false);
                    this->current_row = this->records.begin();
                }
                else {
                    break;
                }
            }

            std::vector<std::string>& temp = *(this->current_row);
            CSVField field;
            DataType dtype;
            bool overflow = false;
            row.clear();

            for (auto it = temp.begin(); it != temp.end(); ++it) {
                dtype = helpers::data_type(*it);

                try {
                    switch (helpers::data_type(*it)) {
                    case _null:   // Empty string
                    case _string:
                        field = CSVField(*it, dtype, false);
                        break;
                    case _int:
                        field = CSVField(std::stoll(*it), dtype, overflow);
                        break;
                    case _float:
                        field = CSVField(std::stold(*it), dtype, overflow);
                        break;
                    }
                }
                catch (std::out_of_range) {
                    // Huge ass number
                    field = CSVField(*it, dtype, true);
                    break;
                }

                row.push_back(field);
            }

            this->current_row++;
            return true;
        }

        return false;
    }

    //
    // CSVField
    //

    int CSVField::is_int() {
        /** Returns:
         *   - 1: If data type is an integer
         *   - -1: If data type is an integer but can't fit in a long long int
         *   - 0: Not an integer
         */
        if (this->dtype == 2) {
            if (this->overflow)
                return -1;
            else
                return 1;
        }
        else {
            return 2;
        }
    }

    int CSVField::is_float() {
        /** Returns:
         *  - 1: If data type is a float
         *  - -1: If data type is a float but can't fit in a long double
         *  - 0: Not a float
         */
        if (this->dtype == 3) {
            if (this->overflow)
                return -1;
            else
                return 1;
        }
        else {
            return 2;
        }
    }

    bool CSVField::is_string() {
        /** Returns True if the field's data type is a string
         * 
         * **Note**: This returns False if the field is an empty string,
         * in which case calling CSVField::is_null() yields True.
        */
        return (this->dtype == 1);
    }

    bool CSVField::is_null() {
        /** Returns True if data type is an empty string */
        return (this->dtype == 0);
    }

    std::string CSVField::get_string() {
        /** Retrieve a string value. If the value is numeric, it will be
         *  type-casted using std::to_string()
         *
         *  **Note**: This can also be used to retrieve empty fields
         */
        if (this->dtype <= 1 || this->overflow) {
            return this->str_data;
        }
        else {
            if (this->dtype == _int)
                return std::to_string(this->get_number<long long int>());
            else
                return std::to_string(this->get_number<long double>());
        }
    }

    long long int CSVField::get_int() {
        /** Shorthand for CSVField::get_number<long long int>() */
        return this->get_number<long long int>();
    }

    long double CSVField::get_float() {
        /** Shorthand for CSVField::get_number<long double>() */
        return this->get_number<long double>();
    }

    namespace helpers {
        std::string json_escape(std::string in) {
            /** Given a CSV string, convert it to a JSON string with proper
             *  escaping as described by RFC 7159
             */

            std::string out;

            for (size_t i = 0, ilen = in.length(); i < ilen; i++) {
                switch (in[i]) {
                case '"':
                    // Assuming quotes come in pairs due to CSV escaping
                    out += "\\\"";
                    i++; // Skip over next quote
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '/':
                    out += "\\/";
                    break;
                case '\r':
                    out += "\\\r";
                    break;
                case '\n':
                    out += "\\\n";
                    break;
                case '\t':
                    out += "\\\t";
                    break;
                default:
                    out += in[i];
                }
            }

            return out;
        }
    }
}