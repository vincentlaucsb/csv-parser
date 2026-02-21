/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#pragma once

#include <algorithm>
#include <deque>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <sstream>
#include <string>
#include <vector>

#include "../external/mio.hpp"
#include "basic_csv_parser.hpp"
#include "common.hpp"
#include "data_type.hpp"
#include "csv_format.hpp"

/** The all encompassing namespace */
namespace csv {
    /** Stuff that is generally not of interest to end-users */
    namespace internals {
        std::string format_row(const std::vector<std::string>& row, csv::string_view delim = ", ");

        std::vector<std::string> _get_col_names( csv::string_view head, const CSVFormat format = CSVFormat::guess_csv());

        struct GuessScore {
            double score;
            size_t header;
        };

        CSV_INLINE GuessScore calculate_score(csv::string_view head, const CSVFormat& format);

        CSVGuessResult _guess_format(csv::string_view head, const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' });
    }

    std::vector<std::string> get_col_names(
        csv::string_view filename,
        const CSVFormat format = CSVFormat::guess_csv());

    /** @brief Guess the delimiter and header row of a CSV file
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] delims    Candidate delimiters to test
     *  @return CSVGuessResult containing the detected delimiter and header row index
     *
     *  **Heuristic:** For each candidate delimiter, calculate a score based on
     *  the most common row length (mode). The delimiter with the highest score wins.
     *  
     *  **Header Detection:**
     *  - If the first row has >= columns than the mode, it's treated as the header
     *  - Otherwise, the first row with the mode length is treated as the header
     *  
     *  This approach handles:
     *  - Headers with trailing delimiters or optional columns (wider than data rows)
     *  - Comment lines before the actual header (first row shorter than mode)
     *  - Standard CSVs where first row is the header
     *  
     *  @note Score = (row_length × count_of_rows_with_that_length)
     */
    CSVGuessResult guess_format(csv::string_view filename,
        const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' });

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
         * 
         * @warning STREAMING CONSTRAINT - DO NOT ATTEMPT TO CACHE ALL DATA
         * This iterator is intentionally std::input_iterator_tag (single-pass) to support
         * streaming large CSV files that may exceed available RAM (e.g., 50+ GB files).
         * 
         * @par CRITICAL DESIGN CONSTRAINT:
         * - The underlying RawCSVData chunks are automatically freed as the iterator advances
         * - Only the CURRENT row's data is kept alive (via CSVRow's RawCSVDataPtr member)
         * - Previous chunks are freed to allow unlimited file sizes with bounded memory
         * 
         * @par WHY FORWARD ITERATOR IS NOT POSSIBLE:
         * - ForwardIterator requires multi-pass guarantees (can hold multiple valid positions)
         * - Supporting this would require caching ALL RawCSVDataPtr chunks in the iterator
         * - This defeats the streaming purpose: a 50 GB CSV would require 50+ GB of RAM
         * - The entire library design depends on automatic chunk cleanup for memory efficiency
         * 
         * @par IMPLICATIONS FOR ALGORITHM USE:
         * - Algorithms requiring ForwardIterator (std::max_element, std::sort, etc.) may
         *   appear to work in tests with small files, but will cause heap-use-after-free
         *   when the algorithm tries to access iterators pointing to freed chunks
         * - CORRECT approach: Copy rows to std::vector first, then use algorithms
         * - Example: auto rows = std::vector<CSVRow>(reader.begin(), reader.end());
         *            auto max_row = std::max_element(rows.begin(), rows.end(), ...);
         * 
         * @par FOR AI AGENTS AND MAINTAINERS:
         * If you encounter heap-use-after-free with ForwardIterator algorithms, the solution
         * is NOT to cache all RawCSVDataPtr chunks - this violates the fundamental streaming
         * architecture. Instead, document the limitation or provide vector-based workarounds.
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
            iterator(CSVReader* reader) : daddy(reader) {}
            iterator(CSVReader*, CSVRow&&);

            /** Access the CSVRow held by the iterator */
            CONSTEXPR_14 reference operator*() { return this->row; }
            CONSTEXPR_14 reference operator*() const { return const_cast<reference>(this->row); }

            /** Return a pointer to the CSVRow the iterator has stopped at */
            CONSTEXPR_14 pointer operator->() { return &(this->row); }
            CONSTEXPR_14 pointer operator->() const { return const_cast<pointer>(&(this->row)); }

            iterator& operator++();   /**< Pre-increment iterator */
            iterator operator++(int); /**< Post-increment iterator */

            /** Returns true if iterators were constructed from the same CSVReader
             *  and point to the same row
             */
            CONSTEXPR bool operator==(const iterator& other) const noexcept {
                return (this->daddy == other.daddy) && (this->i == other.i);
            }

            CONSTEXPR bool operator!=(const iterator& other) const noexcept { return !operator==(other); }
        private:
            CSVReader * daddy = nullptr;  // Pointer to parent
            CSVRow row;                   // Current row
            size_t i = 0;               // Index of current row
        };

        /** @name Constructors
         *  Constructors for iterating over large files and parsing in-memory sources.
         */
         ///@{
        /** @brief Construct CSVReader from filename using memory-mapped I/O
         * 
         * CODE PATH 1 of 2: Uses MmapParser with mio library for maximum performance.
         * This is fundamentally different from the stream-based constructor below.
         * 
         * @note Bugs can exist in this path independently of the stream path (and vice versa)
         * @note When writing tests that validate I/O behavior, BOTH paths must be tested
         * @see StreamParser for the alternative implementation
         */
        CSVReader(csv::string_view filename, CSVFormat format = CSVFormat::guess_csv());

        /** @brief Construct CSVReader from std::istream
         * 
         * CODE PATH 2 of 2: Uses StreamParser with different internal implementation than
         * the memory-mapped constructor above. Issue #281 was specific to THIS path only.
         *
         *  @tparam TStream An input stream deriving from `std::istream`
         *  @note CSV format guessing works differently here - must manually specify dialect
         *  @note When writing tests that validate I/O behavior, BOTH paths must be tested
         *  @see MmapParser for the memory-mapped alternative
         */
        template<typename TStream,
            csv::enable_if_t<std::is_base_of<std::istream, TStream>::value, int> = 0>
        CSVReader(TStream &source, CSVFormat format = CSVFormat::guess_csv()) : _format(format) {
            auto head = internals::get_csv_head(source);
            using Parser = internals::StreamParser<TStream>;

            if (format.guess_delim()) {
                auto guess_result = internals::_guess_format(head, format.possible_delimiters);
                format.delimiter(guess_result.delim);
                // Only override header if user hasn't explicitly called no_header()
                // Note: column_names() also sets header=-1, but it populates col_names,
                // so we can distinguish: no_header() means header=-1 && col_names.empty()
                if (format.header != -1 || !format.col_names.empty()) {
                    format.header = guess_result.header_row;
                }
                this->_format = format;
            }

            if (!format.col_names.empty())
                this->set_col_names(format.col_names);

            this->parser = std::unique_ptr<Parser>(
                new Parser(source, format, col_names)); // For C++11
            this->initial_read();
        }
        ///@}

        CSVReader(const CSVReader&) = delete;             ///< Not copyable
        CSVReader(CSVReader&&) = delete;                  ///< Not movable: contains std::mutex
        CSVReader& operator=(const CSVReader&) = delete;  ///< Not copyable
        CSVReader& operator=(CSVReader&&) = delete;       ///< Not movable: contains std::mutex
        ~CSVReader() {
            if (this->read_csv_worker.joinable()) {
                this->read_csv_worker.join();
            }
        }

        /** @name Retrieving CSV Rows */
        ///@{
        bool read_row(CSVRow &row);
        iterator begin();
        CSV_CONST iterator end() const noexcept;

        /** Returns true if we have reached end of file */
        bool eof() const noexcept { return this->parser->eof(); }
        ///@}

        /** @name CSV Metadata */
        ///@{
        CSVFormat get_format() const;
        std::vector<std::string> get_col_names() const;
        int index_of(csv::string_view col_name) const;
        ///@}

        /** @name CSV Metadata: Attributes */
        ///@{
        /** Whether or not the file or stream contains valid CSV rows,
         *  not including the header.
         *
         *  @note Gives an accurate answer regardless of when it is called.
         *
         */
        CONSTEXPR bool empty() const noexcept { return this->n_rows() == 0; }

        /** Retrieves the number of rows that have been read so far */
        CONSTEXPR size_t n_rows() const noexcept { return this->_n_rows; }

        /** Whether or not CSV was prefixed with a UTF-8 bom */
        bool utf8_bom() const noexcept { return this->parser->utf8_bom(); }
        ///@}

    protected:
        /**
         * \defgroup csv_internal CSV Parser Internals
         * @brief Internals of CSVReader. Only maintainers and those looking to
         *        extend the parser should read this.
         * @{
         */

        /** Sets this reader's column names and associated data */
        void set_col_names(const std::vector<std::string>&);

        /** @brief Set the size of chunks to read from the CSV in bytes
         *
         *  @param[in] size Chunk size in bytes (minimum: 10MB, default: 10MB)
         *  @throws std::invalid_argument if size < 10MB (ITERATION_CHUNK_SIZE)
         *
         *  Use this to handle CSV files where a single row exceeds the default 10MB chunk size.
         *  Larger chunks use more memory but allow parsing of larger individual rows.
         *
         *  Example:
         *  @snippet tests/test_edge_cases_large_rows.cpp Set Chunk Size Example
         *
         *  @note Chunk size must be at least ITERATION_CHUNK_SIZE (10MB) to avoid
         *  architectural constraints and ensure reliable parsing behavior.
         */
        void set_chunk_size(size_t size) {
            if (size < internals::ITERATION_CHUNK_SIZE) {
                throw std::invalid_argument(
                    "Chunk size must be at least " +
                    std::to_string(internals::ITERATION_CHUNK_SIZE) +
                    " bytes (10MB). Provided: " + std::to_string(size)
                );
            }
            this->_chunk_size = size;
        }

        /** @name CSV Settings **/
        ///@{
        CSVFormat _format;
        ///@}

        /** @name Parser State */
        ///@{
        /** Pointer to a object containing column information */
        internals::ColNamesPtr col_names = std::make_shared<internals::ColNames>();

        /** Helper class which actually does the parsing */
        std::unique_ptr<internals::IBasicCSVParser> parser = nullptr;

        /** Queue of parsed CSV rows */
        std::unique_ptr<RowCollection> records{new RowCollection(100)};

        size_t n_cols = 0;  /**< The number of columns in this CSV */
        size_t _n_rows = 0; /**< How many rows (minus header) have been read so far */

        /** @name Multi-Threaded File Reading Functions */
        ///@{
        bool read_csv(size_t bytes = internals::ITERATION_CHUNK_SIZE);
        ///@}

        /**@}*/

    private:
        /** Whether or not rows before header were trimmed */
        bool header_trimmed = false;

        /** @name Multi-Threaded File Reading: Flags and State */
        ///@{
        std::thread read_csv_worker; /**< Worker thread for read_csv() */
        size_t _chunk_size = internals::ITERATION_CHUNK_SIZE; /**< Current chunk size in bytes */
        bool _read_requested = false; /**< Flag to detect infinite read loops (Issue #218) */
        ///@}

        /** If the worker thread throws, store it here and rethrow on the consumer thread. */
        std::exception_ptr read_csv_exception = nullptr;
        std::mutex read_csv_exception_lock;

        void set_read_csv_exception(std::exception_ptr eptr) {
            std::lock_guard<std::mutex> lock(this->read_csv_exception_lock);
            this->read_csv_exception = std::move(eptr);
        }

        std::exception_ptr take_read_csv_exception() {
            std::lock_guard<std::mutex> lock(this->read_csv_exception_lock);
            auto eptr = this->read_csv_exception;
            this->read_csv_exception = nullptr;
            return eptr;
        }

        void rethrow_read_csv_exception_if_any() {
            if (auto eptr = this->take_read_csv_exception()) {
                std::rethrow_exception(eptr);
            }
        }

        /** Read initial chunk to get metadata */
        void initial_read() {
            this->read_csv_worker = std::thread(&CSVReader::read_csv, this, this->_chunk_size);
            this->read_csv_worker.join();
            this->rethrow_read_csv_exception_if_any();
        }

        void trim_header();
    };
}
