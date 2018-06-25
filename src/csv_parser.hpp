#pragma once
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
#include <memory> // For CSVField
#include <limits> // For CSVField

#define CSV_TYPE_CHECK(X) if (this->type_num<X>() != this->type()) \
    throw std::runtime_error("Attempted to convert a value of type " \
        + helpers::type_name(this->type()) + " to " + \
        helpers::type_name(this->type_num<X>()) + ".")

//! The all encompassing namespace
namespace csv {
    /** @file */

    class CSVField;
    using CSVRow = std::vector<CSVField>;

    const int CSV_NOT_FOUND = -1;

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
        int n_rows;                         /**< Number of rows in a file */
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

    /** Enumerates the different CSV field types that are
     *  recognized by this library
     *  
     *  - 0. CSV_NULL (empty string)
     *  - 1. CSV_STRING
     *  - 2. CSV_INT
     *  - 3. CSV_LONG_INT
     *  - 4. CSV_LONG_LONG_INT
     *  - 5. CSV_DOUBLE
     *
     *  **Note**: Overflowing integers will be stored and classified as doubles.
     *  Furthermore, the same number may either be a CSV_LONG_INT or CSV_INT depending on
     *  compiler and platform.
     */
    enum DataType {
        CSV_NULL,
        CSV_STRING,
        CSV_INT,
        CSV_LONG_INT,
        CSV_LONG_LONG_INT,
        CSV_DOUBLE
    };

    /**
    * @namespace csv::helpers
    * @brief Helper functions for various parts of the main library
    */
    namespace helpers {
        bool is_equal(double a, double b, double epsilon = 0.001);
        std::string type_name(const DataType& dtype);
        std::string format_row(const std::vector<std::string>& row, const std::string& delim = ", ");
        DataType data_type(const std::string&, long double * const out = nullptr);
    }

    /** A data type for representing CSV values that have been type-casted. Internally,
     *  the CSVField stores the original string representation of a value, along with 
     *  the casted value as one of int, long, long long, or double. The get() method
     *  provides the main means of retrieving values.
     */
    class CSVField {
        struct CSVFieldConcept {
            CSVFieldConcept(const std::string& value) : str_value(value) {};
            virtual ~CSVFieldConcept() {};
            virtual DataType type() const = 0;
            std::string str_value;
        };

        template<typename T> struct CSVFieldModel : CSVFieldConcept {
            CSVFieldModel(const T& t, const std::string& str) :
                value(t), CSVFieldConcept(str) {};
            CSVFieldModel(const std::string& str) : CSVFieldConcept(str) {};

            T value;
            DataType type() const { return CSVField::type_num<T>(); };
        };

        std::shared_ptr<CSVFieldConcept> value;

    public:
        template<typename T> CSVField(const T& val, const std::string& str_val) :
            value(new CSVFieldModel<T>(val, str_val)) {
            value->str_value = str_val;
        };

        CSVField(const std::nullptr_t&) : value(new CSVFieldModel<std::nullptr_t>("")) {};
        CSVField(const std::string& str_val) : value(new CSVFieldModel<std::string>(str_val)) {};

        template<typename T> T get() const {
            /** Returns the value casted to the requested type, performing type checking before.
             *  An std::runtime_error will be thrown if a type mismatch occurs, with the exception
             *  of T = std::string, in which the original string representation is always returned.
             *  Converting long ints to ints will be checked for overflow.
             *
             *  **Valid options for T**:
             *   - std::string
             *   - int
             *   - long
             *   - long long
             *   - double
             *   - long double
             */
            if (this->is_int()) return this->get_int<T>();
            else CSV_TYPE_CHECK(T);
            return std::static_pointer_cast<CSVFieldModel<T>>(value)->value;
        }

        CSVField() : CSVField(std::nullptr_t()) {}; // Default constructor
        DataType type() const {
            /** Return the type number of the stored value in accordance with the DataType enum */
            return value.get()->type();
        }
        bool is_null() const { return (type() == 0); }
        bool is_str() const { return (type() == 1); }
        bool is_num() const { return (type() > 1); }
        bool is_int() const { return (type() >= CSV_INT && type() <= CSV_LONG_LONG_INT); }
        bool is_float() const { return (type() == CSV_DOUBLE); };

        friend class CSVReader; // So CSVReader::read_row() can create CSVFields

    private:
        template<typename T> T get_int() const {
            /** Return integer values */
            auto int_type = this->type();

            if (int_type > this->type_num<T>())
                throw std::runtime_error("Overflow error");

            switch (int_type) {
            case CSV_INT:
                return static_cast<T>(std::static_pointer_cast<CSVFieldModel<int>>(value)->value);
                break;
            case CSV_LONG_INT:
                return static_cast<T>(std::static_pointer_cast<CSVFieldModel<long int>>(value)->value);
                break;
            case CSV_LONG_LONG_INT:
            default:
                return static_cast<T>(std::static_pointer_cast<CSVFieldModel<long long int>>(value)->value);
                break;
            }
        }

        template<typename T> static DataType type_num();
    };

    // type_num() specializations

    template<> inline DataType CSVField::type_num<int>() { return CSV_INT; }
    template<> inline DataType CSVField::type_num<long int>() { return CSV_LONG_INT; }
    template<> inline DataType CSVField::type_num<long long int>() { return CSV_LONG_LONG_INT; }
    template<> inline DataType CSVField::type_num<double>() { return CSV_DOUBLE; }
    template<> inline DataType CSVField::type_num<long double>() { return CSV_DOUBLE; }
    template<> inline DataType CSVField::type_num<std::nullptr_t>() { return CSV_NULL; }
    template<> inline DataType CSVField::type_num<std::string>() { return CSV_STRING; }

    // get() specializations

    template<>
    inline std::string CSVField::get<std::string>() const {
        /** Returns the original string representation of a field */
        return this->value->str_value;
    }

    template<>
    inline double CSVField::get<double>() const {
        CSV_TYPE_CHECK(double);
        return static_cast<double>(std::static_pointer_cast<CSVFieldModel<long double>>(value)->value);
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
            CSVReader(
                std::string filename,
                std::vector<int> _subset = {},
                CSVFormat format = GUESS_CSV);

            CSVReader(
                CSVFormat format = DEFAULT_CSV,
                std::vector<int>_subset = {});
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
            void feed(const std::string &in);
            void end_feed();
            ///@}

            /** @name Retrieving CSV Rows */
            ///@{
            bool read_row(std::vector<std::string> &row);
            bool read_row(std::vector<CSVField> &row);
            ///@}

            /** @name CSV Metadata */
            ///@{
            const CSVFormat get_format() const;
            const std::vector<std::string> get_col_names() const;
            const int index_of(const std::string& col_name) const;
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
            void clear();
            ///@}

            /** @name Low Level CSV Input Interface
             *  Lower level functions for more advanced use cases
             */
            ///@{
            std::deque<std::vector<std::string>> records
                = {}; /**< Queue of parsed CSV rows */
            inline bool eof() { return !(this->infile); };
            void close();               /**< Close the open file handler */
            ///@}

            friend std::deque<std::vector<std::string>> parse_to_string(
                const std::string&, CSVFormat format);

        protected:
            inline std::string csv_to_json(std::vector<std::string>&);
            void set_col_names(const std::vector<std::string>&);
            std::vector<std::string>              /**< Buffer for row being parsed */
                record_buffer = { std::string() };
            std::deque<std::vector<std::string>>::iterator current_row; /* < Used in read_row() */
            bool current_row_set = false;                               /* Flag to reset iterator */
            bool read_row_check();                                      /* Helper function for read_row */

            /** @name CSV Parsing Callbacks
             *  The heart of the CSV parser. 
             *  These functions are called by feed(std::string&).
             */
            ///@{
            void process_possible_delim(const std::string::const_iterator&, std::string&);
            void process_quote(std::string::const_iterator&,
                std::string::const_iterator&, std::string&);
            void process_newline(std::string::const_iterator&, std::string&);
            void write_record(std::vector<std::string>&);
            virtual void bad_row_handler(std::vector<std::string>);
            ///@}
            
            /** @name CSV Settings and Flags **/
            ///@{
            char delimiter;                /**< Delimiter character */
            char quote_char;               /**< Quote character */
            bool quote_escape = false;     /**< Parsing flag */
            int header_row;                /**< Line number of the header row (zero-indexed) */
            bool strict = false;           /**< Strictness of parser */
            ///@}

            /** @name Column Information */
            ///@{
            std::vector<std::string> col_names; /**< Column names */
            std::vector<int> subset;         /**< Indices of columns to subset */
            std::vector<std::string> subset_col_names;
            bool subset_flag = false; /**< Set to true if we need to subset data */
            ///@}

            /** @name Multi-Threaded File Reading: Worker Thread */
            ///@{
            void read_csv(std::string filename, int nrows = -1, bool close = true);
            void read_csv_worker();
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
            void end_feed();
            std::vector<long double> get_mean();
            std::vector<long double> get_variance();
            std::vector<long double> get_mins();
            std::vector<long double> get_maxes();
            std::vector< std::unordered_map<std::string, int> > get_counts();
            std::vector< std::unordered_map<int, int> > get_dtypes();

            CSVStat(std::string filename, std::vector<int> subset = {},
                StatsOptions options = ALL_STATS, CSVFormat format = GUESS_CSV);
            CSVStat(CSVFormat format = DEFAULT_CSV, std::vector<int> subset = {},
                StatsOptions options = ALL_STATS) : CSVReader(format, subset) {};
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
        CSVGuesser(const std::string _filename) : filename(_filename) {};
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
    std::deque<std::vector<std::string>> parse_to_string(
        const std::string& in, CSVFormat format = DEFAULT_CSV);
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