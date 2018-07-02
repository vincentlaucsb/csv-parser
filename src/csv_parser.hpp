#pragma once
#include <string_view>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <unordered_map>
#include <stdexcept>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <functional>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iterator>

#include "memory.hpp"
#include "data_type.h"
#include "csv_row.hpp"

/** @file */

/** @namespace csv
 *  @brief The all encompassing namespace
 */
namespace csv {
    /** @brief Integer indicating a requested column wasn't found. */
    const int CSV_NOT_FOUND = -1;

    /** @brief Used for counting number of rows */
    using RowCount = long long int;
   
    class CSVRow;
    using CSVCollection = std::deque<CSVRow>;

    /** 
     *  @brief Stores information about how to parse a CSV file
     *
     *   - Can be used to initialize a csv::CSVReader() object
     *   - The preferred way to pass CSV format information between functions
     *
     *  @see csv::DEFAULT_CSV, csv::GUESS_CSV
     *
     */
    struct CSVFormat {
        char delim;
        char quote_char;

        /**< @brief Row number with columns (ignored if col_names is non-empty) */
        int header;

        /**< @brief Should be left empty unless file doesn't include header */
        std::vector<std::string> col_names;

        /**< @brief RFC 4180 non-compliance -> throw an error */
        bool strict;
    };

    /** Returned by get_file_info() */
    struct CSVFileInfo {
        std::string filename;               /**< Filename */
        std::vector<std::string> col_names; /**< CSV column names */
        char delim;                         /**< Delimiting character */
        RowCount n_rows;                    /**< Number of rows in a file */
        int n_cols;                         /**< Number of columns in a CSV */
    };

    /** @namespace csv::internals
     *  @brief Stuff that is generally not of interest to end-users
     */
    namespace internals {
        bool is_equal(double a, double b, double epsilon = 0.001);
        std::string type_name(const DataType& dtype);
        std::string format_row(const std::vector<std::string>& row, const std::string& delim = ", ");
    }

    /** @name Global Constants */
    ///@{
    /** @brief For functions that lazy load a large CSV, this determines how
     *         many bytes are read at a time
     */
    const size_t ITERATION_CHUNK_SIZE = 10000000; // 10MB

    /** @brief A dummy variable used to indicate delimiter should be guessed */
    const CSVFormat GUESS_CSV = { '\0', '"', 0, {}, false };

    /** @brief RFC 4180 CSV format */
    const CSVFormat DEFAULT_CSV = { ',', '"', 0, {}, false };

    /** @brief RFC 4180 CSV format with strict parsing */
    const CSVFormat DEFAULT_CSV_STRICT = { ',', '"', 0, {}, true };
    ///@}

    /** @class CSVReader
     *  @brief Main class for parsing CSVs from files and in-memory sources
     *
     *  All rows are compared to the column names for length consistency
     *  - By default, rows that are too short or too long are dropped
     *  - Custom behavior can be defined by overriding bad_row_handler in a subclass
     */
    class CSVReader {
        public:
            /**
             * @brief An input iterator capable of handling large files.
             * Created by CSVReader::begin() and CSVReader::end().
             *
             * **Iterating over a file:**
             * \snippet tests/test_csv_iterator.cpp CSVReader Iterator 1
             *
             * **Using with <algorithm> library:**
             * \snippet tests/test_csv_iterator.cpp CSVReader Iterator 2
             */
            class iterator {
            public:
                using value_type = CSVRow;
                using difference_type = std::ptrdiff_t;
                using pointer = CSVRow * ;
                using reference = CSVRow & ;
                using iterator_category = std::input_iterator_tag;

                iterator() = default;
                iterator(CSVReader* reader) : daddy(reader) {};
                iterator(CSVReader*, CSVRow&&);

                reference operator*();
                pointer operator->();
                iterator& operator++(); // Pre-inc
                iterator operator++(int); // Post-inc
                iterator& operator--();

                bool operator==(const iterator&) const;
                bool operator!=(const iterator& other) const { return !operator==(other); }

            private:
                CSVReader * daddy = nullptr;  // Pointer to parent
                CSVRow row;                   // Current row
                RowCount i = 0;               // Index of current row
            };

            /** @name Constructors
             *  Constructors for iterating over large files and parsing in-memory sources.
             */
            ///@{
            CSVReader(const std::string& filename, CSVFormat format = GUESS_CSV);
            CSVReader(CSVFormat format = DEFAULT_CSV);
            ///@}

            CSVReader(const CSVReader&) = delete; // No copy constructor
            CSVReader(CSVReader&&) = default;     // Move constructor
            CSVReader& operator=(const CSVReader&) = delete; // No copy assignment
            CSVReader& operator=(CSVReader&& other) = default;
            ~CSVReader() { this->close(); }

            /** @name Reading In-Memory Strings
             *  You can piece together incomplete CSV fragments by calling feed() on them
             *  before finally calling end_feed().
             *
             *  Alternatively, you can also use the parse() shorthand function for
             *  smaller strings.
             */
            ///@{
            void feed(std::string_view in);
            void end_feed();
            ///@}

            /** @name Retrieving CSV Rows */
            ///@{
            bool read_row(CSVRow &row);
            iterator begin();
            iterator end();
            ///@}

            /** @name CSV Metadata */
            ///@{
            CSVFormat get_format() const;
            std::vector<std::string> get_col_names() const;
            int index_of(const std::string& col_name) const;
            ///@}

            /** @name CSV Metadata: Attributes */
            ///@{
            RowCount row_num = 0;        /**< @brief How many lines have
                                          *    been parsed so far
                                          */
            RowCount correct_rows = 0;   /**< @brief How many correct rows
                                          *    (minus header) have been parsed so far
                                          */
            ///@}

            void close();               /**< @brief Close the open file handle.
                                        *   Automatically called by ~CSVReader().
                                        */

            friend CSVCollection parse(const std::string&, CSVFormat);
        protected:
            /**
             * \defgroup csv_internal CSV Parser Internals
             * @brief Internals of CSVReader. Only maintainers and those looking to
             *        extend the parser should read this.
             * @{
             */

            /**  @typedef ParseFlags
             *   @brief   An enum used for describing the significance of each character
             *            with respect to CSV parsing
             */
            enum ParseFlags {
                NOT_SPECIAL,
                QUOTE,
                DELIMITER,
                NEWLINE
            };

            std::vector<CSVReader::ParseFlags> make_flags() const;

            std::string record_buffer = ""; /**<
                @brief Buffer for current row being parsed */

            std::vector<size_t> split_buffer; /**<
                @brief Positions where current row is split */

            size_t min_row_len = (size_t)INFINITY; /**<
                @brief Shortest row seen so far; used to determine how much memory
                       to allocate for new strings */

            std::deque<CSVRow> records; /**< @brief Queue of parsed CSV rows */
            inline bool eof() { return !(this->infile); };

            /** @name CSV Parsing Callbacks
             *  The heart of the CSV parser. 
             *  These methods are called by feed().
            */
            ///@{
            void process_possible_delim(std::string_view);
            void process_quote(std::string_view);
            void process_newline(std::string_view);
            void write_record();
            virtual void bad_row_handler(std::vector<std::string>);
            ///@}

            /** @name CSV Settings **/
            ///@{
            char delimiter;                /**< @brief Delimiter character */
            char quote_char;               /**< @brief Quote character */
            int header_row;                /**< @brief Line number of the header row (zero-indexed) */
            bool strict = false;           /**< @brief Strictness of parser */

            std::vector<CSVReader::ParseFlags> parse_flags; /**< @brief
            A table where the (i + 128)th slot gives the ParseFlags for ASCII character i */
            ///@}

            /** @name Parser State */
            ///@{
            bool quote_escape = false;     /**< @brief Are we currently in a quote escaped field? */
            size_t c_pos = 0;              /**< @brief Position in current string of parser */
            size_t n_pos = 0;              /**< @brief Position in new string (record_buffer) of parser */

            /** <@brief Pointer to a object containing column information
            */
            std::shared_ptr<internals::ColNames> col_names =
                std::make_shared<internals::ColNames>(std::vector<std::string>({}));
            ///@}

            /** @name Multi-Threaded File Reading Functions */
            ///@{
            void feed(std::unique_ptr<std::string>&&); /**< @brief Helper for read_csv_worker() */
            void read_csv(
                const std::string& filename,
                const size_t& bytes = ITERATION_CHUNK_SIZE,
                bool close = true
            );
            void read_csv_worker();
            ///@}

            /** @name Multi-Threaded File Reading: Flags and State */
            ///@{
            std::FILE* infile = nullptr;        /**< @brief Current file handle.
                                                     Destroyed by ~CSVReader(). */

            std::deque<std::unique_ptr<std::string>>
                feed_buffer;                    /**< @brief Message queue for worker */

            std::mutex feed_lock;               /**< @brief Allow only one worker to write */
            std::condition_variable feed_cond;  /**< @brief Wake up worker */
            ///@}

            /**@}*/ // End of parser internals
    };
    
    /** @class CSVStat
     *  @brief Class for calculating statistics from CSV files and in-memory sources
     *
     *  **Example**
     *  \include programs/csv_stats.cpp
     *
     */
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

            CSVStat(std::string filename, CSVFormat format = GUESS_CSV);
            CSVStat(CSVFormat format = DEFAULT_CSV) : CSVReader(format) {};
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
            void count(CSVField&, const size_t&);
            void min_max(const long double&, const size_t&);
            void dtype(CSVField&, const size_t&);

            void calc();
            void calc_worker(const size_t&);
    };

    namespace internals {
        /** Class for guessing the delimiter & header row number of CSV files */
        class CSVGuesser {
            struct Guesser : public CSVReader {
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
    }

    /** @name Shorthand Parsing Functions
     *  @brief Convienience functions for parsing small strings
     */
    ///@{
    CSVCollection operator ""_csv(const char*, size_t);
    CSVCollection parse(const std::string& in, CSVFormat format = DEFAULT_CSV);
    ///@}

    /** @name Utility Functions */
    ///@{
    CSVFileInfo get_file_info(const std::string& filename);
    CSVFormat guess_format(const std::string& filename);
    std::vector<std::string> get_col_names(
        const std::string& filename,
        const CSVFormat format = GUESS_CSV);
    int get_col_pos(const std::string filename, const std::string col_name,
        const CSVFormat format = GUESS_CSV);
    ///@}
}