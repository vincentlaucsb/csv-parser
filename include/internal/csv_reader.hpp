#pragma once
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
#include "giant_string_buffer.hpp"

/** @namespace csv
 *  @brief The all encompassing namespace
 */
namespace csv {
    /** @brief Integer indicating a requested column wasn't found. */
    const int CSV_NOT_FOUND = -1;

    /** @namespace csv::internals
     *  @brief Stuff that is generally not of interest to end-users
     */
    namespace internals {
        std::string type_name(const DataType& dtype);
        std::string format_row(const std::vector<std::string>& row, const std::string& delim = ", ");
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
        void feed(csv::string_view in);
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
        bool utf8_bom = false;       /**< @brief Set to true if UTF-8 BOM was detected */
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

        internals::GiantStringBuffer record_buffer; /**<
            @brief Buffer for current row being parsed */

        std::vector<size_t> split_buffer; /**<
            @brief Positions where current row is split */

        std::deque<CSVRow> records; /**< @brief Queue of parsed CSV rows */
        inline bool eof() { return !(this->infile); };

        /** @name CSV Parsing Callbacks
         *  The heart of the CSV parser.
         *  These methods are called by feed().
        */
        ///@{
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
        /** <@brief Pointer to a object containing column information
        */
        std::shared_ptr<internals::ColNames> col_names =
            std::make_shared<internals::ColNames>(std::vector<std::string>({}));

        /** <@brief Whether or not an attempt to find Unicode BOM has been made */
        bool unicode_bom_scan = false;
        ///@}

        /** @name Multi-Threaded File Reading Functions */
        ///@{
        void feed(std::unique_ptr<char[]>&&); /**< @brief Helper for read_csv_worker() */
        void read_csv(
            const std::string& filename,
            const size_t& bytes = ITERATION_CHUNK_SIZE
        );
        void read_csv_worker();
        ///@}

        /** @name Multi-Threaded File Reading: Flags and State */
        ///@{
        std::FILE* infile = nullptr;         /**< @brief Current file handle.
                                                  Destroyed by ~CSVReader(). */

        std::deque<std::unique_ptr<char[]>>
            feed_buffer;                     /**< @brief Message queue for worker */

        std::mutex feed_lock;                /**< @brief Allow only one worker to write */
        std::condition_variable feed_cond;   /**< @brief Wake up worker */
        ///@} 

        /**@}*/ // End of parser internals
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
}