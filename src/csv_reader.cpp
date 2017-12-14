#include "csv_parser.h"
#include <algorithm>
#include <random>

namespace csv_parser {
    /** @file */

    char guess_delim(std::string filename) {
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
        int max_cols = 0;

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
    
    std::vector<std::string> get_col_names(std::string filename, CSVFormat format) {
        /** Return a CSV's column names */
        CSVReader reader(filename, format);
        reader.close();
        return reader.get_col_names();
    }

    int get_col_pos(std::string filename, std::string col_name, CSVFormat format) {
        /** Resolve column position from column name */
        std::vector<std::string> col_names = get_col_names(filename, format);

        auto it = std::find(col_names.begin(), col_names.end(), col_name);
        if (it != col_names.end())
            return it - col_names.begin();
        else
            return -1;
    }

    CSVFileInfo get_file_info(std::string filename) {
        /** Get basic information about a CSV file */
        CSVReader reader(filename);
        CSVFormat format = reader.get_format();
        std::vector<std::string> row;
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
        /** Create a CSVReader over a file */
        if (format.delim == '\0')
            delimiter = guess_delim(filename);
        else
            delimiter = format.delim;

        quote_char = format.quote_char;
        header_row = format.header;
        subset = _subset;

        // Begin reading CSV
        read_csv(filename, 100, false);
        this->current_row = this->records.begin();
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
        /** - Set or override the CSV's column names
         *  - When parsing, rows that are shorter or longer than the list 
         *    of column names get dropped
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

    void CSVReader::feed(std::string &in) {
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

    void CSVReader::process_possible_delim(std::string &in, size_t &index, std::string* &out) {
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

    void CSVReader::process_newline(std::string &in, size_t &index, std::string* &out) {
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

    void CSVReader::process_quote(std::string &in, size_t &index, std::string* &out) {
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

    std::vector<std::string> CSVReader::pop(bool front) {
        /** - Remove and return the first CSV row
         *  - Considering using empty() to avoid popping from an empty queue
         */

        std::vector<std::string> record;

        if (front) {
            record = this->records.front();
            this->records.pop_front();
        }
        else {
            record = this->records.back();
            this->records.pop_back();
        }

        return record;
    }

    std::map<std::string, std::string> CSVReader::pop_map(bool front) {
        /** - Remove and return the first CSV row as a std::map
         *  - Considering using empty() to avoid popping from an empty queue
         */
        std::vector< std::string > record = this->pop(front);
        std::map< std::string, std::string > record_map;

        for (size_t i = 0; i < subset.size(); i++)
            record_map[this->subset_col_names[i]] = record[i];

        return record_map;
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
            
            if (data_type(record[i]) > 1)
                json_record += record[i];
            else
                json_record += "\"" + json_escape(record[i]) + "\"";

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
        /** Iterate over a potentially larger than RAM file 
         *  storing the parsed results in row
         *
         *  Returns True if file is still open, False otherwise
         */
        while (true) {
            if (this->current_row == this->records.end()) {
                if (!this->eof) {
                    this->clear();
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

    bool CSVReader::read_row(std::vector<void*> &row, 
        std::vector<int> &dtypes,
        bool *overflow)
    {
        /** Same as read_row(std::string, std::vector<std::string> &row) but
         *  does type-casting on values
         */

        while (true) {
            if (this->current_row == this->records.end()) {
                if (!this->eof) {
                    this->clear();
                    this->read_csv("", ITERATION_CHUNK_SIZE, false);
                    this->current_row = this->records.begin();
                }
                else {
                    break;
                }
            }

            std::vector<std::string>& temp = *(this->current_row);
            int dtype;
            void * field;
            row.clear();
            dtypes.clear();

            for (auto it = temp.begin(); it != temp.end(); ++it) {
                dtype = data_type(*it);
                dtypes.push_back(dtype);

                try {
                    switch (data_type(*it)) {
                    case 0: // Empty string
                    case 1: // String
                        field = new std::string(*it);
                        break;
                    case 2: // Integer
                        field = new long long int(std::stoll(*it));
                        break;
                    case 3: // Float
                        field = new long double(std::stold(*it));
                        break;
                    }

                    if (overflow)
                        *overflow = false;
                }
                catch (std::out_of_range) {
                    // Huge ass number
                    dtypes.pop_back();
                    dtypes.push_back(1);
                    field = new std::string(*it);
                    break;

                    if (overflow)
                        *overflow = true;
                }

                row.push_back(field);
            }
            
            this->current_row++;
            return true;
        }

        return false;
    }

    bool CSVReader::read_row(std::vector<CSVField> &row) {
        /** Like read_row(std::vector<void*> &row, std::vector<int>) but safer */
        std::vector<void*> in_row;
        std::vector<CSVField> out_row;
        std::vector<int> dtypes;
        bool overflow;

        while (this->read_row(in_row, dtypes, &overflow)) {
            out_row.clear();
            for (size_t i = 0; i < in_row.size(); i++)
                out_row.push_back(CSVField(in_row[i], dtypes[i], overflow));
            row.swap(out_row);
            return true;
        }

        return false;
    }
    
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

    /** CSVField */
    int CSVField::is_int() {
        /** Returns:
          *  - 1: If data type is an integer
          *  - -1: If data type is an integer but can't fit in a long long int
          *  - 0: Not an integer
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
        /** Returns True if data type is a string */
        return (this->dtype == 1);
    }

    bool CSVField::is_null() {
        /** Returns True if data type is an empty string */
        return (this->dtype == 0);
    }

    std::string CSVField::get_string() {
        if (this->dtype <= 1 || this->overflow) {
            std::string* ptr = (std::string*)this->data_ptr;
            std::string ret = *ptr;
            delete ptr;
            return ret;
        }
        else {
            throw std::runtime_error("[TypeError] Not a string.");
        }
    }

    long long int CSVField::get_int() {
        if (this->dtype == 2) {
            if (!this->overflow) {
                long long int* ptr = (long long int*)this->data_ptr;
                long long int ret = *ptr;
                delete ptr;
                return ret;
            }
            else {
                throw std::runtime_error("[TypeError] Integer overflow: Use get_string(() instead.");
            }
        }
        else {
            throw std::runtime_error("[TypeError] Not an integer.");
        }
    }

    long double CSVField::get_float() {
        if (this->dtype == 3) {
            if (!this->overflow) {
                long double* ptr = (long double*)this->data_ptr;
                long double ret = *ptr;
                delete ptr;
                return ret;
            }
            else {
                throw std::runtime_error("[TypeError] Float overflow: Use get_string(() instead.");
            }
        }
        else {
            throw std::runtime_error("[TypeError] Not a float.");
        }
    }
}