/** @csv */

#include <stdexcept>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <functional>
#include <algorithm>
#include <string>
#include <vector>
#include <deque>
#include <math.h>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace csv {
    /** @file */

    /** Stores information about how to parse a CSV file
     *   - Can be used to initialize a csv::CSVReader() object
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

    /** Enumerates the different CSV field types that are
     *  recognized by this library
     *  
     *  - 0. Null (empty string)
     *  - 1. String
     *  - 2. Integer
     *  - 3. Floating Point Number
     */
    enum DataType {
        _null,
        _string,
        _int,
        _float
    };

    /** A data type for representing CSV values that have been type-casted */
    class CSVField {
    public:
        /** @name Type Information */
        ///@{
        bool is_null();
        bool is_string();
        int is_int();
        int is_float();
        DataType dtype; /**< Store this field's data type enumeration as given by csv::DataType */
        ///@}

        /** @name Value Retrieval */
        ///@{
        std::string get_string();
        long long int get_int();
        long double get_float();
        template <typename T>
        inline T get_number() {
            /** Safely retrieve an integral or floating point value, throwing an error if
            *  the field is not an number or type-casting will cause an overflow.
            *
            *  **Note:** Integer->float and float->integer conversions are implicity performed.
            */
            if (!this->overflow) {
                if (this->dtype == _int)
                    return this->int_data;
                else if (this->dtype == _float)
                    return this->dbl_data;
                else
                    throw std::runtime_error("[TypeError] Not a number.");
            }
            else {
                throw std::runtime_error("[TypeError] Overflow: Use get_string() instead.");
            }
        }
        ///@}

        friend class CSVReader; // So CSVReader::read_row() can create CSVFields

    private:
        /** Construct a CSVField from a std::string */
        CSVField(const std::string data = "", const DataType _type = _null, const bool _overflow = false) :
            dtype(_type), str_data(data), overflow(_overflow) {};

        /** Construct a CSVField from an int */
        CSVField(const long long int data, const DataType _type = _int, const bool _overflow = false) :
            dtype(_type), int_data(data), overflow(_overflow) {};

        /** Construct a CSVField from double */
        CSVField(const long double data, const DataType _type = _float, const bool _overflow = false) :
            dtype(_type), dbl_data(data), overflow(_overflow) {};

        std::string str_data;
        long long int int_data;
        long double dbl_data;
        bool overflow;
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
    std::string csv_escape(const std::string&, const bool quote_minimal=true);
    char guess_delim(const std::string filename);
    std::vector<std::string> get_col_names(
        const std::string filename,
        const CSVFormat format=GUESS_CSV);
    int get_col_pos(const std::string filename, const std::string col_name,
        const CSVFormat format = GUESS_CSV);
    CSVFileInfo get_file_info(const std::string filename);
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

            ~CSVReader();

            /** @name Reading In-Memory Strings
             *  You can piece together incomplete CSV fragments by calling feed() on them
             *  before finally calling end_feed()
             */
            ///@{
            void feed(const std::string &in);
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
            inline void process_possible_delim(const std::string::const_iterator&, std::string&);
            inline void process_quote(std::string::const_iterator&, std::string&);
            inline void process_newline(std::string::const_iterator&, std::string&);
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
            std::vector< std::unordered_map<std::string, int> > get_counts();
            std::vector< std::unordered_map<int, int> > get_dtypes();
            using CSVReader::CSVReader;
        private:
            // An array of rolling averages
            // Each index corresponds to the rolling mean for the column at said index
            std::vector<long double> rolling_means;
            std::vector<long double> rolling_vars;
            std::vector<long double> mins;
            std::vector<long double> maxes;
            std::vector<std::unordered_map<std::string, int>> counts;
            std::vector<std::unordered_map<int, int>> dtypes;
            std::vector<float> n;
            
            // Statistic calculators
            void variance(long double&, size_t&);
            void count(std::string&, size_t&);
            void min_max(long double&, size_t&);
            void dtype(std::string&, size_t&);
            void calc_col(size_t);
    };

    /** Class for writing CSV files.
     *
     *  See csv::csv_escape() for a function that formats a non-CSV string.
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

    /** @name Editing Functions
     *  Functions for editing existing CSV files
     */
    ///@{
    void reformat(std::string infile, std::string outfile, int skiplines = 0);
    void merge(std::string outfile, std::vector<std::string> in);
    ///}

    /**
     * @namespace csv::helpers
     * @brief Helper functions for various parts of the main library
     */
    namespace helpers {
        /** @name Data Type Inference */
        ///@{
        DataType data_type(std::string&);
        ///@}

        /** @name JSON Support */
        ///@{
        std::string json_escape(const std::string&);
        ///@}
    }
}