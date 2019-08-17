/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#pragma once
#include <array>
#include <deque>
#include <iterator>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>

#include "constants.hpp"
#include "data_type.h"
#include "csv_format.hpp"
#include "csv_row.hpp"
#include "compatibility.hpp"
#include "row_buffer.hpp"

/** The all encompassing namespace */
namespace csv {
    /** Integer indicating a requested column wasn't found. */
    constexpr int CSV_NOT_FOUND = -1;

    /** Stuff that is generally not of interest to end-users */
    namespace internals {
        std::string type_name(const DataType& dtype);
        std::string format_row(const std::vector<std::string>& row, csv::string_view delim = ", ");
    }

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
         * An input iterator capable of handling large files.
         * @note Created by CSVReader::begin() and CSVReader::end().
         *
         * @par Iterating over a file
         * @snippet tests/test_csv_iterator.cpp CSVReader Iterator 1
         *
         * @par Using with `<algorithm>` library
         * @snippet tests/test_csv_iterator.cpp CSVReader Iterator 2
         */
        class iterator {
        public:
            #ifndef DOXYGEN_SHOULD_SKIP_THIS
            using value_type = CSVRow;
            using difference_type = std::ptrdiff_t;
            using pointer = CSVRow * ;
            using reference = CSVRow & ;
            using iterator_category = std::input_iterator_tag;
            #endif

            iterator() = default;
            iterator(CSVReader* reader) : daddy(reader) {};
            iterator(CSVReader*, CSVRow&&);

            reference operator*();
            pointer operator->();
            iterator& operator++();   /**< Pre-increment iterator */
            iterator operator++(int); /**< Post-increment ierator */
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
        CSVReader(csv::string_view filename, CSVFormat format = CSVFormat::GUESS_CSV);
        CSVReader(CSVFormat format = CSVFormat());
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
        void feed(csv::string_view in);
        void end_feed();
        ///@}

        /** @name Retrieving CSV Rows */
        ///@{
        bool read_row(CSVRow &row);
        iterator begin();
        HEDLEY_CONST iterator end() const;
        ///@}

        /** @name CSV Metadata */
        ///@{
        CSVFormat get_format() const;
        std::vector<std::string> get_col_names() const;
        int index_of(csv::string_view col_name) const;
        ///@}
        
        /** @name CSV Metadata: Attributes */
        ///@{
        RowCount row_num = 0;        /**< How many lines have been parsed so far */
        RowCount correct_rows = 0;   /**< How many correct rows (minus header)
                                      *   have been parsed so far
                                      */
        bool utf8_bom = false;       /**< Set to true if UTF-8 BOM was detected */
        ///@}

        void close();

        friend CSVCollection parse(csv::string_view, CSVFormat);
    protected:
        /**
         * \defgroup csv_internal CSV Parser Internals
         * @brief Internals of CSVReader. Only maintainers and those looking to
         *        extend the parser should read this.
         * @{
         */

         /**  @typedef ParseFlags
          *   An enum used for describing the significance of each character
          *   with respect to CSV parsing
          */
        enum ParseFlags {
            NOT_SPECIAL, /**< Characters with no special meaning */
            QUOTE,       /**< Characters which may signify a quote escape */
            DELIMITER,   /**< Characters which may signify a new field */
            NEWLINE      /**< Characters which may signify a new row */
        };

        /** A string buffer and its size. Consumed by read_csv_worker(). */
        using WorkItem = std::pair<std::unique_ptr<char[]>, size_t>;

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        HEDLEY_CONST CONSTEXPR
            std::array<CSVReader::ParseFlags, 256> make_parse_flags() const;

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character c and, v[i + 128] is true if 
         *  c is a whitespace character
         */
        HEDLEY_CONST CONSTEXPR
            std::array<bool, 256> make_ws_flags(const char * delims, size_t n_chars) const;

        /** Open a file for reading. Implementation is compiler specific. */
        void fopen(csv::string_view filename);

        /** Sets this reader's column names and associated data */
        void set_col_names(const std::vector<std::string>&);

        /** Returns true if we have reached end of file */
        bool eof() { return !(this->infile); };

        /** Buffer for current row being parsed */
        internals::BufferPtr record_buffer = internals::BufferPtr(new internals::RawRowBuffer());

        /** Queue of parsed CSV rows */
        std::deque<CSVRow> records;

        /** @name CSV Parsing Callbacks
         *  The heart of the CSV parser.
         *  These methods are called by feed().
         */
        ///@{
        void write_record();

        /** Handles possible Unicode byte order mark */
        CONSTEXPR void handle_unicode_bom(csv::string_view& in);
        virtual void bad_row_handler(std::vector<std::string>);
        ///@}

        /** @name CSV Settings **/
        ///@{
        char delimiter;         /**< Delimiter character */
        char quote_char;        /**< Quote character */
        int header_row;         /**< Line number of the header row (zero-indexed) */
        bool strict = false;    /**< Strictness of parser */

        /** An array where the (i + 128)th slot gives the ParseFlags for ASCII character i */
        std::array<ParseFlags, 256> parse_flags;

        /** An array where the (i + 128)th slot determines whether ASCII character i should
         *  be trimmed
         */
        std::array<bool, 256> ws_flags;
        ///@}

        /** @name Parser State */
        ///@{
        /** Pointer to a object containing column information */
        internals::ColNamesPtr col_names = std::make_shared<internals::ColNames>(
            std::vector<std::string>({}));

        /** Whether or not an attempt to find Unicode BOM has been made */
        bool unicode_bom_scan = false;

        /** Whether or not we have parsed the header row */
        bool header_was_parsed = false;

        /** The number of columns in this CSV */
        size_t n_cols = 0;
        ///@}

        /** @name Multi-Threaded File Reading Functions */
        ///@{
        void feed(WorkItem&&); /**< @brief Helper for read_csv_worker() */
        void read_csv(const size_t& bytes = internals::ITERATION_CHUNK_SIZE);
        void read_csv_worker();
        ///@}

        /** @name Multi-Threaded File Reading: Flags and State */
        ///@{
        std::FILE* HEDLEY_RESTRICT
            infile = nullptr;                /**< Current file handle.
                                                  Destroyed by ~CSVReader(). */
        std::deque<WorkItem> feed_buffer;    /**< Message queue for worker */
        std::mutex feed_lock;                /**< Allow only one worker to write */
        std::condition_variable feed_cond;   /**< Wake up worker */
        ///@} 

        /**@}*/ // End of parser internals
    };

    namespace internals {
        /** Class for guessing the delimiter & header row number of CSV files */
        class CSVGuesser {

            /** Private subclass of csv::CSVReader which performs statistics 
             *  on row lengths
             */
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
            CSVGuesser(csv::string_view _filename, const std::vector<char>& _delims) :
                filename(_filename), delims(_delims) {};
            CSVGuessResult guess_delim();
            bool first_guess();
            void second_guess();

        private:
            std::string filename;      /**< File to read */
            std::string head;          /**< First x bytes of file */
            std::vector<char> delims;  /**< Candidate delimiters */

            char delim;                /**< Chosen delimiter (set by guess_delim()) */
            int header_row = 0;        /**< Chosen header row (set by guess_delim()) */

            void get_csv_head();       /**< Retrieve the first x bytes of a file */
        };
    }
}