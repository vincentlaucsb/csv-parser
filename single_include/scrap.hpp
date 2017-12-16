#include <stdexcept>
#include <set>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <regex>
#include <iostream>
#include <math.h>
#include <vector>
#include <deque>
#include <thread>
#include <random>
#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <mutex>
using std::vector;
using std::string;

/** @csv_parser */


namespace csv_parser {
    /** @file */

    /** Stores information about how to parse a CSV file
     *   - Can be used to initialize a csv_parser::CSVReader() object
     *   - The preferred way to pass CSV format information between functions
     */
    struct CSVFormat {
        char delim;
        char quote_char;
        int header;
    };

    /** Returned by get_file_info() */
    struct CSVFileInfo {
        std::string filename;
        std::vector<std::string> col_names;
        char delim;
        int n_rows;
        int n_cols;
    };

    /** A data type for representing CSV values that have been type-casted
     *  that is more sophisticated than a bare void * pointer
     */
    class CSVField {
    public:
        CSVField(void * _data_ptr, int _type, bool _overflow = false) :
            data_ptr(_data_ptr), dtype(_type), overflow(_overflow) {};

        bool is_null();
        bool is_string();
        int is_int();
        int is_float();
        std::string get_string();
        long long int get_int();
        long double get_float();
    private:
        void * data_ptr;
        bool overflow;
        int dtype;
    };

    /** @name Global Constants */
    ///@{
    /** For functions that lazy load a large CSV, this determines how
     *  many rows are read at a time
     */
    const size_t ITERATION_CHUNK_SIZE = 100000;

    /** A dummy variable used to indicate delimiter should be guessed */
    const CSVFormat GUESS_CSV = { '\0', '"', 0 };
    ///@}

    /** @name Utility Functions
      * Functions for getting quick information from CSV files 
      * without writing a lot of code
      */
    ///@{
    std::string csv_escape(std::string&, bool quote_minimal=true);
    char guess_delim(std::string filename);
    std::vector<std::string> get_col_names(std::string filename,
        CSVFormat format=GUESS_CSV);
    int get_col_pos(std::string filename, std::string col_name,
        CSVFormat format = GUESS_CSV);
    CSVFileInfo get_file_info(std::string filename);
    ///@}

    /** The main class for parsing CSV files
     *
     *  CSV data can be read in the following ways
     *  -# From in-memory strings using feed() and end_feed()
     *  -# From CSV files using the multi-threaded read_csv() function
     *
     *  All rows are compared to the column names for length consistency
     *  - By default, rows that are too short or too long are dropped
     *  - A custom callback can be registered by setting bad_row_handler
     */
    class CSVReader {        
        public:
            /** @name Constructors
             *  There are two constructors, both suited for different purposes
             *  
             * - **Iterating Over a File**
             *   - CSVReader(std::string, CSVFormat, std::vector<int>)
             *     allows one to lazily read a potentially larger than
             *     RAM CSV file with just a few lines of code
             * - **More General Usage**
             *   - CSVReader(char, char, int, std::vector<int>) is
             *     more flexible and can be used to parse in-memory
             *     strings or read entire files into memory
             */
            ///@{
            CSVReader(
                std::string filename,
                CSVFormat format = GUESS_CSV,
                std::vector<int> _subset = {});

            CSVReader(
                char _delim = ',',
                char _quote = '"',
                int _header = 0,
                std::vector<int>_subset = {}) :
                delimiter(_delim), quote_char(_quote), header_row(_header), subset(_subset) {};
            ///@}

            /** @name Reading In-Memory Strings
             *  You can piece together incomplete CSV fragments by calling feed() on them
             *  before finally calling end_feed()
             */
            ///@{
            void feed(std::string &in);
            void end_feed();
            ///@}

            /** @name Reading CSV Files
             *  **Note**: It is generally unnecessary to call this if you used the 
             *  CSVReader(std::string, CSVFormat, std::vector<int>) constructor
             */
            ///@{
            void read_csv(std::string filename, int nrows = -1, bool close = true);
            ///@}

            /** @name Retrieving CSV Rows */
            ///@{
            bool read_row(std::vector<std::string> &row);
            bool read_row(std::vector<void*> &row,
                std::vector<int> &dtypes, bool *overflow = nullptr);
            bool read_row(std::vector<CSVField> &row);
            ///@}

            /** @name CSV Metadata */
            ///@{
            CSVFormat get_format();
            std::vector<std::string> get_col_names();
            ///@}

            /** @name CSV Metadata: Attributes */
            ///@{
            int row_num = 0;        /**< How many lines have been parsed so far */
            int correct_rows = 0;   /**< How many correct rows (minus header) have been parsed so far */
                                    ///@}

            /** @name Output
             *  Functions for working with parsed CSV rows
             */
            ///@{
            void to_json(std::string filename, bool append = false);
            std::vector<std::string> to_json();
            void clear();
            bool empty();
            //void sample(int n);
            ///@}

            /** @name Low Level CSV Input Interface
             *  Lower level functions for more advanced use cases
             */
            ///@{
            void set_col_names(std::vector<std::string>);
            std::string infile_name;
            bool eof = false;        /**< Have we reached the end of file */
            void close();            /**< Close the open file handler */
            ///@}

            /** @name Parsing Callbacks */
            ///@{
            void(*bad_row_handler)(std::vector<std::string>) =
                nullptr; /**< Callback for rows that are too short */
            ///@}

            std::deque<std::vector<std::string>>::iterator begin() {
                /** Return an iterator over the rows CSVReader has parsed so far */
                return this->records.begin();
            }

            std::deque<std::vector<std::string>>::iterator end() {
                /** Return an iterator pointing the the last parsed row */
                return this->records.end();
            }

        protected:
            /** @name CSV Parsing Callbacks
             *  The heart of the CSV parser. 
             *  These functions are called by feed(std::string&).
             */
            ///@{
            inline void process_possible_delim(std::string&, size_t&, std::string*&);
            inline void process_quote(std::string&, size_t&, std::string*&);
            inline void process_newline(std::string&, size_t&, std::string*&);
            inline void write_record(std::vector<std::string>&);
            ///@}
                        
            // Helper methods
            inline std::string csv_to_json(std::vector<std::string>&);
            
            /** @name CSV Settings and Flags **/
            ///@{
            char delimiter;                /**< Delimiter character */
            char quote_char;               /**< Quote character */
            bool quote_escape = false;     /**< Parsing flag */
            int header_row;                /**< Line number of the header row (zero-indexed) */
            ///@}

            /** @name Buffers **/
            ///@{
            std::deque< std::vector
                <std::string>> records;           /**< Queue of parsed CSV rows */
            std::vector<std::string>              /**< Buffer for row being parsed */
                record_buffer = { std::string() };
            ///@}

            /** @name Column Information */
            ///@{
            std::vector<std::string> col_names; /**< Column names */
            std::vector<int> subset; /**< Indices of columns to subset */
            std::vector<std::string> subset_col_names;
            bool subset_flag = false; /**< Set to true if we need to subset data */
            ///@}

            /** @name Flags for read_row() */
            ///@{
            std::deque<std::vector<std::string>>::iterator current_row;
            bool read_start = false;
            ///@}

            /** @name Multi-Threaded File Reading: Worker Thread */
            ///@{
            void _read_csv();                     /**< Worker thread for read_csv() */
            ///@}

            /** @name Multi-Threaded File Reading */
            ///@{
            std::FILE* infile = nullptr;
            std::deque<std::string*> feed_buffer; /**< Message queue for worker */
            std::mutex feed_lock;                 /**< Allow only one worker to write */
            std::condition_variable feed_cond;    /**< Wake up worker */
            ///@}
    };
    
    /** Class for calculating statistics from CSV files */
    class CSVStat: public CSVReader {
        public:
            void calc(bool numeric=true, bool count=true, bool dtype=true);
            void calc_csv(std::string filename, bool numeric=true, bool count=true, bool dtype=true);
            std::vector<long double> get_mean();
            std::vector<long double> get_variance();
            std::vector<long double> get_mins();
            std::vector<long double> get_maxes();
            std::vector< std::map<std::string, int> > get_counts();
            std::vector< std::map<int, int> > get_dtypes();
            using CSVReader::CSVReader;
        protected:
            // Statistic calculators
            void dtype(std::string&, size_t&);
            
            // Map column indices to counters
            std::map<int, std::map<int, int>> dtypes;
        private:
            // An array of rolling averages
            // Each index corresponds to the rolling mean for the column at said index
            std::vector<long double> rolling_means;
            std::vector<long double> rolling_vars;
            std::vector<long double> mins;
            std::vector<long double> maxes;
            std::vector<float> n;
            
            // Statistic calculators
            void variance(long double&, size_t&);
            void count(std::string&, size_t&);
            void min_max(long double&, size_t&);
            void calc_col(size_t);
            
            // Map column indices to counters
            std::map<int, std::map<std::string, int>> counts;
    };

    /** Class for writing CSV files.
     *
     *  See csv_parser::csv_escape() for a function that formats a non-CSV string.
     *
     *  To write to a CSV file, one should
     *   -# Initialize a CSVWriter with respect to some file
     *   -# Call write_row() on std::vector<std::string>s of unformatted text
     *   -# close() the file handle
     */
    class CSVWriter {
        public:
            void write_row(std::vector<std::string> record, bool quote_minimal=true);
            void close();
            CSVWriter(std::string filename);
        private:
            std::ofstream outfile;
    };

    /**
     * @namespace csv_parser::extra
     * @brief CSV reading/editing goodies built on top of the main library
     */
    namespace extra {
        /** @name Search Functions */
        ///@{
        void head(std::string infile, int nrow = 100, std::vector<int> subset = {});
        void grep(std::string infile, int col, std::string match, int max_rows = 500);
        ///@}

        /** @name Editing Functions
         *  Functions for editing existing CSV files
         */
        void reformat(std::string infile, std::string outfile, int skiplines = 0);
        void merge(std::string outfile, std::vector<std::string> in);

        /** @name SQLite Functions
         *  Functions built using the SQLite3 API
         */
        ///@{
        void csv_to_sql(std::string csv_file, std::string db,
            std::string table = "");
        void csv_join(std::string filename1, std::string filename2, std::string outfile,
            std::string column1 = "", std::string column2 = "");
        ///@}
    }

    /**
     * @namespace csv_parser::helpers
     * @brief Helper functions for various parts of the main library
     */
    namespace helpers {
        /** @name Data Type Inference */
        ///@{
        int data_type(std::string&);
        ///@}

        /** @name Path Handling */
        ///@{
        std::vector<std::string> path_split(std::string);
        std::string get_filename_from_path(std::string path);
        ///@}

        /** @name JSON Support */
        ///@{
        std::string json_escape(std::string);
        ///@}
    }

    /**
     * @namespace csv_parser::sql
     * @brief Helper functions for SQL-related functionality
     */
    namespace sql {
        /** @name SQL Functions */
        ///@{
        std::string sql_sanitize(std::string);
        std::vector<std::string> sql_sanitize(std::vector<std::string>);
        std::vector<std::string> sqlite_types(std::string filename, int nrows = 50000);
        ///@}

        /** @name Dynamic SQL Generation */
        ///@{
        std::string create_table(std::string, std::string);
        std::string insert_values(std::string, std::string);
        ///@}
    }
}



namespace csv_parser {
    /** @file */

    namespace extra {
        void reformat(std::string infile, std::string outfile, int skiplines) {
            /** Reformat a CSV file
             *  @param[in]  infile    Path to existing CSV file
             *  @param[out] outfile   Path to file to write to
             *  @param[out] skiplines Number of lines past header to skip
             */

            CSVReader reader(infile);
            CSVWriter writer(outfile);
            vector<string> row;
            writer.write_row(reader.get_col_names());

            while (reader.read_row(row)) {
                for (; skiplines > 0; skiplines--)
                    reader.read_row(row);
                writer.write_row(row);
            }

            writer.close();
        }

        void merge(std::string outfile, std::vector<std::string> in) {
            /** Merge several CSV files together
             *  @param[out] outfile Path to file to write to
             *  @param[in]  in      Vector of paths to input files
             */
            std::set<string> col_names = {};
            vector<string> first_col_names;
            vector<string> temp_col_names;

            for (string infile : in) {
                /* Make sure columns are the same across all files
                 * Currently assumes header is on first line
                 */

                if (col_names.empty()) {
                    // Read first CSV
                    first_col_names = get_col_names(infile);

                    for (string cname : first_col_names) {
                        col_names.insert(cname);
                    }
                }
                else {
                    temp_col_names = get_col_names(infile);

                    if (temp_col_names.size() < col_names.size()) {
                        throw std::runtime_error("Inconsistent columns.");
                    }

                    /*
                    for (string cname: temp_col_names) {
                    }
                    */
                }
            }

            // Begin merging
            CSVWriter writer(outfile);
            vector<string> row;

            for (string infile : in) {
                CSVReader reader(infile);
                while (reader.read_row(row))
                    writer.write_row(row);
            }

            writer.close();
        }
    }
}

namespace csv_parser {
    /** @file
     *  Defines all functionality needed for basic CSV parsing
     */

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
    
    std::vector<std::string> get_col_names(std::string filename, CSVFormat format) {
        /** Return a CSV's column names
         *  @param[in] filename  Path to CSV file
         *  @param[in] format    Format of the CSV file
         */
        CSVReader reader(filename, format);
        reader.close();
        return reader.get_col_names();
    }

    int get_col_pos(std::string filename, std::string col_name, CSVFormat format) {
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

    CSVFileInfo get_file_info(std::string filename) {
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

    bool CSVReader::read_row(std::vector<void*> &row, 
        std::vector<int> &dtypes,
        bool *overflow)
    {
        /** Same as read_row(std::string, std::vector<std::string> &row) but
         *  does type-casting on values. Consider using 
         *  CSVReader::read_row(std::vector<CSVField> &row)
         *  for a safer alternative.
         *
         *  @param[out] row      Where the row's values will be stored
         *  @param[out] dtypes   Used for storing data type information
         *  @param[out] overflow If specified, used to indicate if previous value
         *                       couldn't be type-casted due to a
         *                       std::out_of_range error
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
            int dtype;
            void * field;
            row.clear();
            dtypes.clear();

            for (auto it = temp.begin(); it != temp.end(); ++it) {
                dtype = helpers::data_type(*it);
                dtypes.push_back(dtype);

                try {
                    switch (helpers::data_type(*it)) {
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
        /** Perform automatic type-casting when retrieving rows
         *  - This is implemented on top of 
         *    read_row(std::vector<void*> &row, std::vector<int> &dtypes)
         *    but is much safer
         *  - This is also much faster and more robust than calling std::stoi()
         *    on the values of a std::vector<std::string>
         *
         *  **Note:** See the documentation for CSVField to see how to work
         *  with the output of this function
         *
         *  @param[out] row A vector of strings where the read row will be stored
         */
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

    //
    // CSVField
    //

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
        /** Returns True if the field's data type is a string
         * 
         * **Note**: This returns False if the field is an empty string,
         * in which case calling is_null() yields True.
        */
        return (this->dtype == 1);
    }

    bool CSVField::is_null() {
        /** Returns True if data type is an empty string */
        return (this->dtype == 0);
    }

    std::string CSVField::get_string() {
        /** Retrieve a string value, throwing an error if the field is 
         *  not a string
         *
         *  **Note**: This can also be used to retrieve empty fields
         */
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
        /** Safely retrieve an integral value, throwing an error if 
         *  the field is not an integer or type-casting will cause an
         *  integer overflow.
         */
        if (this->dtype == 2) {
            if (!this->overflow) {
                long long int* ptr = (long long int*)this->data_ptr;
                long long int ret = *ptr;
                delete ptr;
                return ret;
            }
            else {
                throw std::runtime_error("[TypeError] Integer overflow: Use get_string() instead.");
            }
        }
        else {
            throw std::runtime_error("[TypeError] Not an integer.");
        }
    }

    long double CSVField::get_float() {
        /** Safely retrieve a floating point value, throwing an error if
         *  the field is not a floating point number or type-casting will
         *  cause an overflow
         */
        if (this->dtype == 3) {
            if (!this->overflow) {
                long double* ptr = (long double*)this->data_ptr;
                long double ret = *ptr;
                delete ptr;
                return ret;
            }
            else {
                throw std::runtime_error("[TypeError] Float overflow: Use get_string() instead.");
            }
        }
        else {
            throw std::runtime_error("[TypeError] Not a float.");
        }
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


namespace csv_parser {
    /** @file
      * Calculates statistics from CSV files
      */

    std::vector<long double> CSVStat::get_mean() {
        /** Return current means */
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->rolling_means[i]);
        }
        return ret;
    }

    std::vector<long double> CSVStat::get_variance() {
        /** Return current variances */
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->rolling_vars[i]/(this->n[i] - 1));
        }
        return ret;
    }

    std::vector<long double> CSVStat::get_mins() {
        /** Return current variances */
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->mins[i]);
        }
        return ret;
    }

    std::vector<long double> CSVStat::get_maxes() {
        /** Return current variances */
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->maxes[i]);
        }
        return ret;
    }

    std::vector< std::map<std::string, int> > CSVStat::get_counts() {
        /** Get counts for each column */
        std::vector< std::map<std::string, int> > ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->counts[i]);
        }
        return ret;
    }

    std::vector< std::map<int, int> > CSVStat::get_dtypes() {
        /** Get data type counts for each column */
        std::vector< std::map<int, int> > ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->dtypes[i]);
        }
        return ret;
    }

    void CSVStat::calc(bool numeric, bool count, bool dtype) {
        /** Go through all records and calculate specified statistics
         *  @param   numeric Calculate all numeric related statistics
         *  @param   count   Create frequency counter for field values
         *  @param   dtype   Calculate data type statistics
         */

        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            rolling_means.push_back(0);
            rolling_vars.push_back(0);
            mins.push_back(NAN);
            maxes.push_back(NAN);
            n.push_back(0);
        }

        vector<std::thread> pool;

        // Start threads
        for (size_t i = 0; i < subset_col_names.size(); i++) {
            pool.push_back(std::thread(&CSVStat::calc_col, this, i));
        }

        // Block until done
        for (auto it = pool.begin(); it != pool.end(); ++it) {
            (*it).join();
        }

        this->clear();
    }

    void CSVStat::calc_csv(std::string filename, bool numeric, bool count, bool dtype) {
        /** Lazily calculate statistics for a potentially very big file. 
         *  This method is a wrapper on top of CSVStat::calc();
         */
        while (!this->eof) {
            this->read_csv(filename, ITERATION_CHUNK_SIZE, false);
            this->calc(numeric, count, dtype);
        }
    }

    void CSVStat::calc_col(size_t i) {
        /** Worker thread which calculates statistics for one column.
         *  Intended to be called from CSVStat::calc().
         * 
         *  @param[out] i Column index
         */

        std::deque<vector<string>>::iterator current_record = this->records.begin();
        long double x_n;

        while (current_record != this->records.end()) {
            this->count((*current_record)[i], i);
            this->dtype((*current_record)[i], i);

            // Numeric Stuff
            try {
                // Using data_type() to check if field is numeric is faster
                // than catching stold() errors
                if (helpers::data_type((*current_record)[i]) >= 2) {
                    x_n = std::stold((*current_record)[i]);

                    // This actually calculates mean AND variance
                    this->variance(x_n, i);
                    this->min_max(x_n, i);
                }
            }
            catch (std::out_of_range) {
                // Ignore for now
            }

            // (*current_record)[i].clear();
            ++current_record;
        }
    }

    void CSVStat::dtype(std::string &record, size_t &i) {
        /** Given a record update the type counter
         *  @param[in]  record Data observation
         *  @param[out] i      The column index that should be updated
         */
        int type = helpers::data_type(record);
        
        if (this->dtypes[i].find(type) !=
            this->dtypes[i].end()) {
            // Increment count
            this->dtypes[i][type]++;
        } else {
            // Initialize count
            this->dtypes[i].insert(std::make_pair(type, 1));
        }
    }

    void CSVStat::count(std::string &record, size_t &i) {
        /** Given a record update the frequency counter
         *  @param[in]  record Data observation
         *  @param[out] i      The column index that should be updated
         */
        if (this->counts[i].find(record) !=
            this->counts[i].end()) {
            // Increment count
            this->counts[i][record]++;
        } else {
            // Initialize count
            this->counts[i].insert(std::make_pair(record, 1));
        }
    }

    void CSVStat::min_max(long double &x_n, size_t &i) {
        /** Update current minimum and maximum
         *  @param[in]  x_n Data observation
         *  @param[out] i   The column index that should be updated
         */
        if (isnan(this->mins[i])) {
            this->mins[i] = x_n;
        } if (isnan(this->maxes[i])) {
            this->maxes[i] = x_n;
        }
        
        if (x_n < this->mins[i]) {
            this->mins[i] = x_n;
        } else if (x_n > this->maxes[i]) {
            this->maxes[i] = x_n;
        } else {
        }
    }

    void CSVStat::variance(long double &x_n, size_t &i) {
        /** Given a record update rolling mean and variance for all columns
         *  using Welford's Algorithm
         *  @param[in]  x_n Data observation
         *  @param[out] i   The column index that should be updated
         */
        long double * current_rolling_mean = &this->rolling_means[i];
        long double * current_rolling_var = &this->rolling_vars[i];
        float * current_n = &this->n[i];
        long double delta;
        long double delta2;
        
        *current_n = *current_n + 1;
        
        if (*current_n == 1) {
            *current_rolling_mean = x_n;
        } else {
            delta = x_n - *current_rolling_mean;
            *current_rolling_mean += delta/(*current_n);
            delta2 = x_n - *current_rolling_mean;
            *current_rolling_var += delta*delta2;
        }
    }
}


namespace csv_parser {
    /** @file */
    std::string csv_escape(std::string& in, bool quote_minimal) {
        /** Format a string to be RFC 4180-compliant
         *  @param[in]  in              String to be CSV-formatted
         *  @param[out] quote_minimal   Only quote fields if necessary.
         *                              If False, everything is quoted.
         */

        std::string new_string = "\""; // Start initial quote escape sequence
        bool quote_escape = false;     // Do we need a quote escape

        for (size_t i = 0; i < in.size(); i++) {
            switch (in[i]) {
            case '"':
                new_string += "\"\"";
                quote_escape = true;
                break;
            case ',':
                quote_escape = true;
                // Do not break;
            default:
                new_string += in[i];
            }
        }

        if (quote_escape || !quote_minimal) {
            new_string += "\""; // Finish off quote escape
            return new_string;
        }
        else {
            return in;
        }
    }

    CSVWriter::CSVWriter(std::string outfile) {
        /** Open a file for writing
         *  @param[out] outfile Path of the file to be written to
         */
        this->outfile = std::ofstream(outfile, std::ios_base::binary);
    }

    void CSVWriter::write_row(vector<string> record, bool quote_minimal) {
        /** Format a sequence of strings and write to CSV according to RFC 4180
         *
         *  **Note**: This does not check to make sure row lengths are consistent
         *  @param[in]  record          Vector of strings to be formatted
         *  @param      quote_minimal   Only quote fields if necessary
         */

        for (size_t i = 0, ilen = record.size(); i < ilen; i++) {
            this->outfile << csv_escape(record[i]);
            if (i + 1 != ilen) 
                this->outfile << ",";
        }

        this->outfile << "\r\n";
    }

    void CSVWriter::close() {
        /** Close the file being written to */
        this->outfile.close();
    }

    /*
    void CSVWriter::to_postgres(std::string filename, bool quote_minimal,
        int skiplines, bool append) {
        /** Generate a PostgreSQL dump file
         *  @param[out] filename        File to save to
         *  @param      skiplines       Number of lines (after header) to skip
         */
        
        // Write queue to CSV file
    /*
        std::string row;
        std::vector<std::string> record;
        std::ofstream outfile;

        while (!this->eof) {
            this->read_csv(filename, 100000, false);
            // this->to_csv(filename, )
        }
    }
*/
}

namespace csv_parser {
    namespace helpers {
        /** @file */

        int data_type(std::string &in) {
            /** Distinguishes numeric from other text values. Used by various 
             *  type casting functions, like csv_parser::CSVReader::read_row()
             * 
             *  #### Return
             *   - 0:  If null (empty string)
             *   - 1:  If string
             *   - 2:  If int
             *   - 3:  If float
             *
             *  #### Rules
             *   - Leading and trailing whitespace ("padding") ignored
             *   - A string of just whitespace is NULL
             *  
             *  @param[in] in String value to be examined
             */

            // Empty string --> NULL
            if (in.size() == 0)
                return 0;

            bool ws_allowed = true;
            bool neg_allowed = true;
            bool dot_allowed = true;
            bool digit_allowed = true;
            bool has_digit = false;
            bool prob_float = false;

            for (size_t i = 0, ilen = in.size(); i < ilen; i++) {
                switch (in[i]) {
                case ' ':
                    if (!ws_allowed) {
                        if (isdigit(in[i - 1])) {
                            digit_allowed = false;
                            ws_allowed = true;
                        }
                        else {
                            // Ex: '510 123 4567'
                            return 1;
                        }
                    }
                    break;
                case '-':
                    if (!neg_allowed) {
                        // Ex: '510-123-4567'
                        return 1;
                    }
                    else {
                        neg_allowed = false;
                    }
                    break;
                case '.':
                    if (!dot_allowed) {
                        return 1;
                    }
                    else {
                        dot_allowed = false;
                        prob_float = true;
                    }
                    break;
                default:
                    if (isdigit(in[i])) {
                        if (!digit_allowed) {
                            return 1;
                        }
                        else if (ws_allowed) {
                            // Ex: '510 456'
                            ws_allowed = false;
                        }
                        has_digit = true;
                    }
                    else {
                        return 1;
                    }
                }
            }

            // No non-numeric/non-whitespace characters found
            if (has_digit) {
                if (prob_float) {
                    return 3;
                }
                else {
                    return 2;
                }
            }
            else {
                // Just whitespace
                return 0;
            }
        }
    }
}


namespace csv_parser {
    /** @file */
    namespace extra {
        void head(std::string infile, int nrow, std::vector<int> subset) {
            /** Print out the first n rows of a CSV */
            CSVReader reader(infile);
            vector<string> row;
            vector<vector<string>> records = {};
            int i = 0;

            while (reader.read_row(row)) {
                if (records.empty())
                    records.push_back(reader.get_col_names());

                records.push_back(row);
                i++;

                if (i%nrow == 0) {
                    helpers::print_table(records, i - nrow);
                    std::cout << std::endl
                        << "Press Enter to continue printing, or q or Ctrl + C to quit."
                        << std::endl << std::endl;
                    if (std::cin.get() == 'q') {
                        reader.close();
                        break;
                    }
                }
            }
        }

        void grep(std::string infile, int col, std::string match, int max_rows) {
            std::regex reg_pattern(match);
            std::smatch matches;
            const int orig_max_rows = max_rows;

            CSVReader reader(infile);
            vector<string> row;
            vector<vector<string>> records = {};

            while (reader.read_row(row)) {
                if (records.empty())
                    records.push_back(reader.get_col_names());

                std::regex_search(row[col], matches, reg_pattern);
                if (!matches.empty()) {
                    records.push_back(row);
                    max_rows--;
                }

                if (max_rows == 0) {
                    helpers::print_table(records);
                    std::cout << std::endl
                        << "Press Enter to continue searching, or q or Ctrl + C to quit."
                        << std::endl << std::endl;

                    if (std::cin.get() == 'q') {
                        reader.close();
                        break;
                    }
                    max_rows = orig_max_rows;
                }
            }
        }
    }
}