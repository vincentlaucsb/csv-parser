#include "csv_parser.h"
#include <random>

namespace csv {
    /** @file
     *  Defines all functionality needed for basic CSV parsing
     */

    namespace helpers {
        /** @file */

        bool is_equal(double a, double b, double epsilon) {
            /** Returns true if two doubles are about the same */
            return std::abs(a - b) < epsilon;
        }

        DataType data_type(const std::string &in, long double* out) {
            /** Distinguishes numeric from other text values. Used by various
             *  type casting functions, like csv_parser::CSVReader::read_row()
             *
             *  #### Rules
             *   - Leading and trailing whitespace ("padding") ignored
             *   - A string of just whitespace is NULL
             *
             *  @param[in] in String value to be examined
             */

            // Empty string --> NULL
            if (in.size() == 0)
                return CSV_NULL;

            bool ws_allowed = true;
            bool neg_allowed = true;
            bool dot_allowed = true;
            bool digit_allowed = true;
            bool has_digit = false;
            bool prob_float = false;

            unsigned places_after_decimal = 0;
            long double num_buff = 0;

            for (size_t i = 0, ilen = in.size(); i < ilen; i++) {
                const char& current = in[i];

                switch (current) {
                case ' ':
                    if (!ws_allowed) {
                        if (isdigit(in[i - 1])) {
                            digit_allowed = false;
                            ws_allowed = true;
                        }
                        else {
                            // Ex: '510 123 4567'
                            return CSV_STRING;
                        }
                    }
                    break;
                case '-':
                    if (!neg_allowed) {
                        // Ex: '510-123-4567'
                        return CSV_STRING;
                    }
                    else {
                        neg_allowed = false;
                    }
                    break;
                case '.':
                    if (!dot_allowed) {
                        return CSV_STRING;
                    }
                    else {
                        dot_allowed = false;
                        prob_float = true;
                    }
                    break;
                default:
                    if (isdigit(current)) {
                        // Process digit
                        has_digit = true;

                        if (!digit_allowed)
                            return CSV_STRING;
                        else if (ws_allowed) // Ex: '510 456'
                            ws_allowed = false;

                        // Build current number
                        unsigned digit = current - '0';
                        if (num_buff == 0) {
                            num_buff = digit;
                        }
                        else if (prob_float) {
                            num_buff += (long double)digit / pow(10.0, ++places_after_decimal);
                        }
                        else {
                            num_buff *= 10;
                            num_buff += digit;
                        }
                    }
                    else {
                        return CSV_STRING;
                    }
                }
            }

            // No non-numeric/non-whitespace characters found
            if (has_digit) {
                if (!neg_allowed) num_buff *= -1;
                if (out) *out = num_buff;

                if (prob_float)
                    return CSV_DOUBLE;
                else {
                    long double log10_num_buff;
                    if (!neg_allowed) log10_num_buff = log10(-num_buff);
                    else log10_num_buff = log10(num_buff);

                    if (log10_num_buff < log10(std::numeric_limits<int>::max()))
                        return CSV_INT;
                    else if (log10_num_buff < log10(std::numeric_limits<long int>::max()))
                        return CSV_LONG_INT;
                    else if (log10_num_buff < log10(std::numeric_limits<long long int>::max()))
                        return CSV_LONG_LONG_INT;
                    else // Conversion to long long will cause an overflow
                        return CSV_DOUBLE;
                }
            }
            else {
                // Just whitespace
                return CSV_NULL;
            }
        }
    }

    CSVFormat guess_format(const std::string filename) {
        CSVGuesser guesser(filename);
        guesser.guess_delim();
        return { guesser.delim, '"', guesser.header_row };
    }

    void CSVReader::bad_row_handler(std::vector<std::string> record) {
        if (this->strict) {
            std::string problem;
            if (record.size() > col_names.size()) problem = "too long";
            else problem = "too short";

            throw std::runtime_error("Line " + problem + " around line " +
                std::to_string(correct_rows) + " near\n" +
                helpers::format_row(record)
            );
        }
    };

    void CSVGuesser::Guesser::bad_row_handler(std::vector<std::string> record) {
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
        char current_delim{','};
        int max_rows = 0;
        int temp_rows = 0;
        size_t max_cols = 0;

        for (size_t i = 0; i < delims.size(); i++) {
            format.delim = delims[i];
            CSVReader guesser(this->filename, {}, format);
           
            // WORKAROUND on Unix systems because certain newlines
            // get double counted
            // temp_rows = guesser.correct_rows;
            temp_rows = std::min(guesser.correct_rows, 100);
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

    std::deque<std::vector<std::string>> parse_to_string(
        const std::string& in, CSVFormat format) {
        /** Parse an in-memory CSV string */
        CSVReader parser(format);
        parser.feed(in);
        parser.end_feed();
        return parser.records;
    }

    std::deque<CSVRow> parse(const std::string& in, CSVFormat format) {
        /** Parse an in-memory CSV string */
        CSVReader parser(format);
        std::deque<CSVRow> ret;
        CSVRow temp;

        parser.feed(in);
        parser.end_feed();

        while (parser.read_row(temp))
            ret.push_back(temp);

        return ret;
    }

    std::vector<std::string> get_col_names(const std::string filename, CSVFormat format) {
        /** Return a CSV's column names
         *  @param[in] filename  Path to CSV file
         *  @param[in] format    Format of the CSV file
         */
        CSVReader reader(filename, {}, format);
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

        CSVReader reader(filename, {}, format);
        return reader.index_of(col_name);
    }

    CSVFileInfo get_file_info(const std::string filename) {
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

    CSVReader::CSVReader(CSVFormat format, std::vector<int> _subset) :
        delimiter(format.delim), quote_char(format.quote_char),
        header_row(format.header), subset(_subset), strict(format.strict) {
        if (!format.col_names.empty()) {
            this->header_row = -1;
            this->set_col_names(format.col_names);
        }
    };

    CSVReader::CSVReader(std::string filename, std::vector<int> _subset,
        CSVFormat format) {
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
            format = guess_format(filename);
            
        delimiter = format.delim;
        quote_char = format.quote_char;
        header_row = format.header;
        subset = _subset;
        strict = format.strict;

        // Begin reading CSV
        read_csv(filename, 100, false);
    }

    CSVReader::~CSVReader() {
        /** Close any open file handles */
        this->close();
    }

    const CSVFormat CSVReader::get_format() const {
        /** Return the format of the original raw CSV */
        return {
            this->delimiter,
            this->quote_char,
            this->header_row,
            this->col_names
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
            for (size_t i = 0; i < this->subset.size(); i++)
                subset_col_names.push_back(col_names[this->subset[i]]);
        } else {
            // "Subset" is every column
            for (size_t i = 0; i < this->col_names.size(); i++)
                this->subset.push_back(i);
            subset_col_names = col_names;
        }
    }

    const std::vector<std::string> CSVReader::get_col_names() const {
        return this->subset_col_names;
    }

    const int CSVReader::index_of(const std::string& col_name) const {
        auto col_names = this->get_col_names();
        for (size_t i = 0; i < col_names.size(); i++)
            if (col_names[i] == col_name) return (int)i;

        return CSV_NOT_FOUND;
    }

    void CSVReader::feed(const std::string &in) {
        /** Parse a CSV-formatted string. Incomplete CSV fragments can be void print_row(const std::vector<std::string>& row);name
         *  joined together by calling feed() on them sequentially.
         *  **Note**: end_feed() should be called after the last string
         */

        auto it_begin = in.begin();
        for (auto it = in.begin(), it_end = in.end(); it != it_end; ++it) {
            if (*it == this->delimiter) {
                this->process_possible_delim(it, this->record_buffer.back());
            } else if (*it == this->quote_char) {
                this->process_quote(it, it_begin, this->record_buffer.back());
            } else {
                switch(*it) {
                    case '\r':
                    case '\n':
                        this->process_newline(it, this->record_buffer.back());
                        break;
                    default:
                        this->record_buffer.back() += *it;
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

    void CSVReader::process_possible_delim(const std::string::const_iterator& in,
        std::string& out) {
        /** Process a delimiter character and determine if it is a field 
         *  separator
         */

        if (!this->quote_escape) // Make a new field
            this->record_buffer.push_back(std::string());
        else // Treat as a regular character
            out += *in;
    }

    void CSVReader::process_newline(std::string::const_iterator &in, std::string &out) {
        /** Process a newline character and determine if it is a record
         *  separator        
         */
        if (!this->quote_escape) {
            // Case: Carriage Return Line Feed, Carriage Return, or Line Feed
            // => End of record -> Write record
            if ((*in == '\r') && (*(in + 1) == '\n'))
                ++in;
            this->write_record(this->record_buffer);
        }
        else { // Treat as a regular character
            out += *in;
        }
    }

    void CSVReader::process_quote(std::string::const_iterator &in,
        std::string::const_iterator& begin,
        std::string &out) {
        /** Determine if the usage of a quote is valid or fix it */
        if (this->quote_escape) {
            if ((*(in + 1) == this->delimiter) || 
                (*(in + 1) == '\r') ||
                (*(in + 1) == '\n')) {
                // Case: End of field
                this->quote_escape = false;
            }
            else {
                out += *in;
                if (*(in + 1) == this->quote_char)
                    ++in;  // Case: Two consecutive quotes
                else if (this->strict)
                    throw std::runtime_error("Unescaped single quote around line " +
                        std::to_string(this->correct_rows) + " near:\n" +
                        std::string((in - 50 < begin ? begin : in - 50), in));
            }
        } else {
             // Case: Previous character was delimiter
             // Don't deref past beginning
            if ((in != begin) && (*(in - 1) == this->delimiter))
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
                if (!record.empty())
                    bad_row_handler(record);
            }
        } else if (this->row_num == this->header_row) {
            this->set_col_names(record);
        } // else: Ignore rows before header row

        record.clear();
        record.push_back(std::string());
        this->row_num++;
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
            if (!this->infile)
                throw std::runtime_error("Cannot open file " + filename);
        }

        char * line_buffer = new char[10000];
        std::string* buffer = new std::string();
        std::thread worker(&CSVReader::_read_csv, this);

        while (nrows <= -1 || nrows > 0) {
            char * result = std::fgets(line_buffer, sizeof(char[10000]), this->infile);            
            if (result == NULL) break;
            else if (std::feof(this->infile)) {
                *buffer += line_buffer;
                break;
            }
            else *buffer += line_buffer;

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
    
    std::string CSVReader::csv_to_json(std::vector<std::string>& record) {
        /** Helper method for both to_json() methods */
        std::string json_record = "{";
        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            json_record += "\"" + helpers::json_escape(this->subset_col_names[i]) + "\":";
            
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

        return json_record += "}";
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
            outfile << this->csv_to_json(*it) << std::endl;

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
        std::uniformCSV_INT_distribution<int> distribution(0, this->records.size() - 1);

        for (; n > 1; n--)
            new_rows.push_back(this->records[distribution(generator)]);

        this->clear();
        this->records.swap(new_rows);
    }
    */

    bool CSVReader::read_row_check() {
        /** Helper function which pulls more data from file if necessary,
         *  and determines when to stop reading
         */
        if (!this->current_row_set) {
            this->current_row_set = true;
            this->current_row = this->records.begin();
        }

        if (this->current_row == this->records.end()) {
            this->clear();

            if (!this->eof()) {
                this->read_csv("", ITERATION_CHUNK_SIZE, false);
                this->current_row = this->records.begin();
            }
            else return false; // Stop reading
        }

        return true;
    }

    bool CSVReader::read_row(std::vector<std::string> &row) {
        /** Retrieve rows parsed by CSVReader in FIFO order.
         *   - If CSVReader was initialized with respect to a file, then this lazily
         *     iterates over the file until no more rows are available.
         *
         *  #### Return Value
         *  Returns True if more rows are available, False otherwise.
         *
         *  #### Alternatives 
         *  If you want automatic type casting of values, then use
         *  CSVReader::read_row(std::vector<CSVField> &row)
         *
         *  @param[out] row A vector of strings where the read row will be stored
         */
        if (this->read_row_check()) {
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
        if (this->read_row_check()) {
            std::vector<std::string>& temp = *(this->current_row);
            CSVField field;
            DataType dtype;
            bool overflow = false;
            long double d_buff;
            row.clear();

            for (auto& record: temp) {
                dtype = helpers::data_type(record, &d_buff);
                switch (dtype) {
                case CSV_NULL:   // Empty string
                    field = CSVField(nullptr);
                    break;
                case CSV_STRING:
                    field = CSVField(record);
                    break;
                case CSV_INT:
                    field = CSVField(static_cast<int>(d_buff), record);
                    break;
                case CSV_LONG_INT:
                    field = CSVField(static_cast<long int>(d_buff), record);
                    break;
                case CSV_LONG_LONG_INT:
                    field = CSVField(static_cast<long long int>(d_buff), record);
                    break;
                case CSV_DOUBLE:
                    field = CSVField(d_buff, record);
                    break;
                }

                row.push_back(field);
            }

            this->current_row++;
            return true;
        }

        return false;
    }

    namespace helpers {
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

        std::string json_escape(const std::string& in) {
            /** Given a CSV string, convert it to a JSON string with proper
             *  escaping as described by RFC 7159
             */

            std::string out;
            out.reserve(in.size());

            for (auto it = in.begin(); it != in.end(); ++it) {
                switch (*it) {
                case '"':
                    out += "\\\"";
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
                    out += *it;
                }
            }

            return out;
        }
    }
}