#pragma once
#include <string_view>
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
#include <sstream>

#include "memory.hpp"
#include "data_type.h"
#include "csv_row.hpp"

#define CSV_TYPE_CHECK(X) if (this->type_num<X>() != this->type()) \
    throw std::runtime_error("Attempted to convert a value of type " \
        + helpers::type_name(this->type()) + " to " + \
        helpers::type_name(this->type_num<X>()) + ".")

//! The all encompassing namespace
namespace csv {
    /** @file */

    const int CSV_NOT_FOUND = -1;
    using RowCount = long long int;

    /** Stores information about how to parse a CSV file
     *   - Can be used to initialize a csv::CSVReader() object
     *   - The preferred way to pass CSV format information between functions
     */
    struct CSVFormat {
        char delim;
        char quote_char;
        int header; /**< Row number with columns
            (ignored if col_names is non-empty) */
        std::vector<std::string> col_names; /**< Should be left empty
            unless file doesn't include header */
        bool strict; /**< RFC 4180 non-compliance -> throw an error */
    };

    /** Returned by get_file_info() */
    struct CSVFileInfo {
        std::string filename;               /**< Filename */
        std::vector<std::string> col_names; /**< CSV column names */
        char delim;                         /**< Delimiting character */
        RowCount n_rows;                    /**< Number of rows in a file */
        int n_cols;                         /**< Number of columns in a CSV */
    };

    /** Tells CSVStat which statistics to calculate 
     *  numeric Calculate all numeric related statistics
     *  count   Create frequency counter for field values
     *  dtype   Calculate data type statistics
     */
    struct StatsOptions {
        bool calc;
        bool numeric;
        bool dtype;
    };

    const StatsOptions ALL_STATS = { true, true, true };

    /**
    * @namespace csv::helpers
    * @brief Helper functions for various parts of the main library
    */
    namespace helpers {
        bool is_equal(double a, double b, double epsilon = 0.001);
        std::string type_name(const DataType& dtype);
        std::string format_row(const std::vector<std::string>& row, const std::string& delim = ", ");
    }

    /** @name Global Constants */
    ///@{
    /** For functions that lazy load a large CSV, this determines how
     *  many rows are read at a time
     */
    const size_t ITERATION_CHUNK_SIZE = 100000;

    /** A dummy variable used to indicate delimiter should be guessed */
    const CSVFormat GUESS_CSV = { '\0', '"', 0, {}, false };

    /** Default CSV format */
    const CSVFormat DEFAULT_CSV = { ',', '"', 0, {}, false },
        DEFAULT_CSV_STRICT = { ',', '"', 0, {}, true };
    ///@}

    /** The main class for parsing CSV files
     *
     *  CSV data can be read in the following ways
     *  -# From in-memory strings using feed() and end_feed()
     *  -# From CSV files using the multi-threaded read_csv() function
     *
     *  All rows are compared to the column names for length consistency
     *  - By default, rows that are too short or too long are dropped
     *  - Custom behavior can be defined by overriding bad_row_handler in a subclass
     */
    class CSVReader {
        public:
            /** @name Constructors */
            ///@{
            CSVReader(std::string filename, CSVFormat format = GUESS_CSV);
            CSVReader(CSVFormat format = DEFAULT_CSV);
            ///@}

            CSVReader(const CSVReader&) = delete; // No copy constructor
            CSVReader(CSVReader&&) = default;     // Move constructor
            CSVReader& operator=(const CSVReader&) = delete; // No copy assignment
            CSVReader& operator=(CSVReader&& other) = default;

            /** @name Reading In-Memory Strings
             *  You can piece together incomplete CSV fragments by calling feed() on them
             *  before finally calling end_feed()
             */
            ///@{
            void feed(std::unique_ptr<std::string>&&);
            void feed(std::string_view in);
            void end_feed();
            ///@}

            /** @name Retrieving CSV Rows */
            ///@{
            bool read_row(CSVRow &row);
            ///@}

            /** @name CSV Metadata */
            ///@{
            CSVFormat get_format() const;
            std::vector<std::string> get_col_names() const;
            int index_of(const std::string& col_name) const;
            ///@}

            /** @name CSV Metadata: Attributes */
            ///@{
            RowCount row_num = 0;        /**< How many lines have been parsed so far */
            RowCount correct_rows = 0;   /**< How many correct rows (minus header) have been parsed so far */
            ///@}

            /** @name Output
             *  Functions for working with parsed CSV rows
             */
            ///@{
            void clear();
            ///@}

            /** @name Low Level CSV Input Interface
             *  Lower level functions for more advanced use cases
             */
            ///@{
            std::deque<CSVRow> records; /**< Queue of parsed CSV rows */
            void close();               /**< Close the open file handler */
            inline bool eof() { return !(this->infile); };
            ///@}

        protected:
            void set_col_names(std::vector<std::string>&);
            std::string record_buffer = "";    /* < Buffer for current row being parsed */
            std::vector<size_t> split_buffer;  /* < Positions where current row is split */
            size_t min_row_len = INFINITY;     /* < Shortest row seen so far */

            /** @name CSV Parsing Callbacks
             *  The heart of the CSV parser. 
             *  These functions are called by feed(std::string&).
             */
            ///@{
            void process_possible_delim(std::string_view);
            void process_quote(std::string_view);
            void process_newline(std::string_view);
            void write_record();
            virtual void bad_row_handler(std::vector<std::string>);
            ///@}
            
            /** @name CSV Settings and Flags **/
            ///@{
            char delimiter;                /**< Delimiter character */
            char quote_char;               /**< Quote character */
            bool quote_escape = false;     /**< Parsing flag */
            int header_row;                /**< Line number of the header row (zero-indexed) */
            bool strict = false;           /**< Strictness of parser */
            size_t c_pos = 0;            /**< Position in current string of parser */
            size_t n_pos = 0;            /**< Position in new string of parser */
            ///@}

            /** @name Column Information */
            ///@{
            std::shared_ptr<ColNames> col_names =
                std::make_shared<ColNames>(std::vector<std::string>({}));
            ///@}

            /** @name Multi-Threaded File Reading: Worker Thread */
            ///@{
            void read_csv(std::string filename, int nrows = -1, bool close = true);
            void read_csv_worker();
            ///@}

            /** @name Multi-Threaded File Reading */
            ///@{
            std::FILE* infile = nullptr;
            std::deque<std::unique_ptr<std::string>> feed_buffer;
                                                /**< Message queue for worker */
            std::mutex feed_lock;               /**< Allow only one worker to write */
            std::condition_variable feed_cond;  /**< Wake up worker */
            ///@}
    };
    
    /** Class for calculating statistics from CSV files */
    class CSVStat: public CSVReader {
        public:
            using FreqCount = std::unordered_map<std::string, RowCount>;
            using TypeCount = std::unordered_map<DataType, RowCount>;

            void end_feed();
            std::vector<long double> get_mean() const;
            std::vector<long double> get_variance() const;
            std::vector<long double> get_mins() const;
            std::vector<long double> get_maxes() const;
            std::vector<FreqCount> get_counts() const;
            std::vector<TypeCount> get_dtypes() const;

            CSVStat(std::string filename, StatsOptions options = ALL_STATS,
                CSVFormat format = GUESS_CSV);
            CSVStat(CSVFormat format = DEFAULT_CSV, StatsOptions options = ALL_STATS)
                : CSVReader(format) {};
        private:
            // An array of rolling averages
            // Each index corresponds to the rolling mean for the column at said index
            std::vector<long double> rolling_means;
            std::vector<long double> rolling_vars;
            std::vector<long double> mins;
            std::vector<long double> maxes;
            std::vector<FreqCount> counts;
            std::vector<TypeCount> dtypes;
            std::vector<long double> n;
            
            // Statistic calculators
            void variance(const long double&, const size_t&);
            void count(const std::string&, const size_t&);
            void min_max(const long double&, const size_t&);
            DataType dtype(const std::string&, const size_t&, long double&);

            void calc(StatsOptions options = ALL_STATS);
            void calc_worker(const size_t);
    };

    /** Class for guessing the delimiter & header row number of CSV files */
    class CSVGuesser {
        struct Guesser: public CSVReader {
            using CSVReader::CSVReader;
            void bad_row_handler(std::vector<std::string> record) override;
            friend CSVGuesser;

            // Frequency counter of row length
            std::unordered_map<size_t, size_t> row_tally = { { 0, 0 } };

            // Map row lengths to row num where they first occurred
            std::unordered_map<size_t, size_t> row_when = { { 0, 0 } };
        };

    public:
        CSVGuesser(const std::string& _filename) : filename(_filename) {};
        std::vector<char> delims = { ',', '|', '\t', ';', '^' };
        void guess_delim();
        bool first_guess();
        void second_guess();

        char delim;
        int header_row = 0;

    private:
        std::string filename;
    };

    /** @name Utility Functions */
    ///@{
    std::deque<CSVRow> parse(const std::string& in, CSVFormat format = DEFAULT_CSV);

    CSVFileInfo get_file_info(const std::string& filename);
    CSVFormat guess_format(const std::string& filename);
    std::vector<std::string> get_col_names(
        const std::string filename,
        const CSVFormat format = GUESS_CSV);
    int get_col_pos(const std::string filename, const std::string col_name,
        const CSVFormat format = GUESS_CSV);
    ///@}
}