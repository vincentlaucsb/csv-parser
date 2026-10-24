/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#pragma once

#include <algorithm>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "common.hpp"
#include "csv_exceptions.hpp"
#include "data_type.hpp"
#include "csv_format.hpp"
#include "parser/mmap.hpp"
#include "parser/scheduler.hpp"
#include "parser/stream.hpp"

/** The all encompassing namespace */
namespace csv {
    /** @class CSVReader
     *  @brief Main class for parsing CSVs from files and in-memory sources
     *
     *  All rows are compared to the column names for length consistency
     *  - By default, rows that are too short or too long are dropped
     *  - Custom behavior can be defined by overriding bad_row_handler in a subclass
     *
     *  **Streaming semantics:** CSVReader is a single-pass streaming reader. Every read
     *  operation — read_row(), the iterator interface — pulls rows permanently
     *  from the internal queue. Rows consumed by one interface are not visible to another.
     *  There is no rewind or seek.
     *
    *  **Ownership and sharing:** CSVReader is non-copyable and move-enabled. It manages
    *  live parsing state (worker thread, internal queue, and optional owned stream), so
    *  ownership transfer should be explicit. To share or transfer a reader, wrap it in a
    *  `std::unique_ptr<CSVReader>`:
     *  @code{.cpp}
     *  auto reader = std::make_unique<csv::CSVReader>("data.csv");
     *  process(std::move(reader));   // transfer ownership
     *  @endcode
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
         * @note This iterator is `std::input_iterator_tag` (single-pass) by design.
         *       Algorithms requiring ForwardIterator are not safe to use directly on it.
         *       Copy to `std::vector<CSVRow>` first when random-access algorithms are needed.
         *       See `include/internal/ARCHITECTURE.md` § "CSVReader::iterator is single-pass by design"
         *       for the full rationale.
         */
#if CSV_EXCEPTIONS_ENABLED
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
#endif

        /** @name Constructors
         *  Constructors for iterating over large files and parsing in-memory sources.
         */
         ///@{
        /** @brief Construct CSVReader from filename.
         * 
         * Native builds use CODE PATH 1 of 2: MmapParser with mio for maximum performance.
         * Emscripten builds fall back to the stream-based implementation because mmap is unavailable.
         *
         * During construction, parser installation performs an initial synchronous metadata
         * read so delimiter and header information are resolved before user reads begin.
         *
         * @note On native builds, bugs can exist in this path independently of the stream path.
         * @note When writing tests that validate I/O behavior, test both filename and stream constructors.
         * @see StreamParser for the stream-based alternative.
         */
        /** Open a reader, returning null on initialization failure.
         *  A borrowed stream must outlive the reader and any pending worker.
         *  In a no-exceptions build it must have a non-throwing exception mask.
         */
        static std::unique_ptr<CSVReader> try_open(
            csv::string_view filename, CSVError& error,
            const CSVFormat& format = CSVFormat::guess_csv());

        template<typename TStream,
            csv::enable_if_t<std::is_base_of<std::istream, TStream>::value, int> = 0>
        static std::unique_ptr<CSVReader> try_open(
            TStream& source, CSVError& error,
            const CSVFormat& format = CSVFormat::guess_csv()) {
            error.clear();
            std::unique_ptr<CSVReader> reader(new CSVReader(format, initialize_tag{}));
            reader->init_from_stream(source, format);
            error = reader->error_;
            return error ? nullptr : std::move(reader);
        }

        static std::unique_ptr<CSVReader> try_open(
            std::unique_ptr<std::istream> source, CSVError& error,
            const CSVFormat& format = CSVFormat::guess_csv());

#ifdef CSV_HAS_STD_EXPECTED
        template<typename Source>
        static std::expected<std::unique_ptr<CSVReader>, CSVError> try_open(
            Source&& source, const CSVFormat& format = CSVFormat::guess_csv()) {
            CSVError error;
            auto reader = try_open(std::forward<Source>(source), error, format);
            if (error) return std::unexpected(std::move(error));
            return reader;
        }
#endif

#if CSV_EXCEPTIONS_ENABLED
        CSVReader(csv::string_view filename, const CSVFormat& format = CSVFormat::guess_csv())
            : CSVReader(format, initialize_tag{}) {
            open_file(filename, format);
            internals::throw_error(error_);
        }

        /** @brief Construct CSVReader from std::istream
         * 
         * Uses StreamParser. On native builds this is CODE PATH 2 of 2 and remains independent
         * from the filename-based mmap path. On Emscripten, the filename constructor also funnels
         * through this implementation.
         *
         *  @tparam TStream An input stream deriving from `std::istream`
         *  @note Delimiter/header guessing is still available by default via CSVFormat::guess_csv().
         *        For deterministic parsing of known dialects, pass an explicit CSVFormat.
         *  @note On native builds, tests that validate I/O behavior should cover both constructors
         *  @see MmapParser for the memory-mapped alternative
         */
        template<typename TStream,
            csv::enable_if_t<std::is_base_of<std::istream, TStream>::value, int> = 0>
        CSVReader(TStream& source, const CSVFormat& format = CSVFormat::guess_csv())
            : CSVReader(format, initialize_tag{}) {
            init_from_stream(source, format);
            internals::throw_error(error_);
        }

        /** @brief Construct CSVReader from an owned std::istream
         *
         *  This is an opt-in safety switch for stream lifetime management.
         *  CSVReader takes ownership and guarantees the stream outlives parsing.
         */
        CSVReader(std::unique_ptr<std::istream> source,
            const CSVFormat& format = CSVFormat::guess_csv())
            : CSVReader(format, initialize_tag{}) {
            owned_stream = std::move(source);
            if (!owned_stream)
                throw std::invalid_argument(internals::ERROR_READER_NULL_STREAM);
            init_from_stream(*owned_stream, format);
            internals::throw_error(error_);
        }
#endif
        ///@}

        CSVReader(const CSVReader&) = delete;             ///< Not copyable
        CSVReader& operator=(const CSVReader&) = delete;  ///< Not copyable

        /** Move constructor.
         *
         * Required so C++11 builds can return CSVReader by value from helpers like
         * csv::parse()/csv::parse_unsafe(), where copy elision is not guaranteed.
         *
         * Any active read scheduler on the source is joined before moving parser state to
         * avoid a thread continuing to run against the source object's address.
         */
        CSVReader(CSVReader&& other) noexcept :
            read_scheduler_(other._format.is_threading_enabled()) {
            other.read_scheduler_.join();
            this->move_state_from(other);
        }

        /** Move assignment.
         *
         * Joins active workers on both sides before transferring parser state.
         */
        CSVReader& operator=(CSVReader&& other) noexcept {
            if (this == &other) {
                return *this;
            }

            this->read_scheduler_.join();
            other.read_scheduler_.join();
            this->move_state_from(other);

            return *this;
        }

        ~CSVReader() {
            this->read_scheduler_.join();
        }

        /** @name Retrieving CSV Rows */
        ///@{
        /** Retrieve the next CSV row, returning `true` while more rows are available.
         *
         * This is the lowest-level row-consumption API on `CSVReader`. Each successful
         * call overwrites `row` with the next parsed record in stream order.
         *
         * @returns `true` if a row was produced, `false` after end-of-stream is reached.
         *
         * @note This permanently consumes rows from the stream.
         *
         * @par Performance Notes
         * - Reads chunks of data that are `csv::internals::CSV_CHUNK_SIZE_DEFAULT`
         *   bytes large at a time by default.
         * - For field-access performance details, read the documentation for
         *   `CSVRow` and `CSVField`.
         *
         * **Example:**
         * \snippet tests/test_read_csv.cpp CSVField Example
         */
#if CSV_EXCEPTIONS_ENABLED
        bool read_row(CSVRow &row);
#endif
        /** Retrieve a row, distinguishing EOF (false, no error) from failure.
         *  A failure is terminal. Successfully consumed rows are not rolled back.
         */
        bool try_read_row(CSVRow& row, CSVError& error);

        /** Read up to `max_rows` rows into a caller-owned batch buffer.
         *
         * This is the easiest way to process a CSV in bounded batches without
         * materializing the entire file. Each call clears `out`, then appends up
         * to `max_rows` newly parsed rows in stream order.
         *
         * @returns `true` if this call produced any rows. Returns `false` only
         *          after end-of-stream is reached and no rows were produced.
         *          A final partial chunk still returns `true`.
         *
         * @param[out] out Destination batch buffer. Existing contents are discarded.
         * @param[in] max_rows Maximum number of rows to place into `out`.
         *
         * @note Like read_row(), this permanently consumes rows from the stream.
         * @note `std::vector<CSVRow>` is intentionally the only supported container:
         *       it matches the public batch-consumption pattern better than the
         *       internal deque-based producer/consumer queue.
         *
         * **Example:**
         * \snippet tests/test_read_csv_file.cpp CSVReader read_chunk Example
         */
#if CSV_EXCEPTIONS_ENABLED
        bool read_chunk(std::vector<CSVRow>& out, size_t max_rows);
        iterator begin();
        CSV_CONST iterator end() const noexcept;
#endif
        /** Clear out and read a batch. Failure may leave a successfully consumed prefix.
         *  max_rows must be nonzero. Inspect error even when out contains rows.
         */
        bool try_read_chunk(std::vector<CSVRow>& out, size_t max_rows, CSVError& error);

        /** Returns true if we have reached end of file */
        bool eof() const noexcept { return this->parser->eof(); }
        ///@}

        /** @name CSV Metadata */
        ///@{
        /** Return the resolved parsing format for this CSV source.
         *
         * The returned format reflects delimiter/header inference and the active
         * column names after construction.
         */
        CSVFormat get_format() const;

        /** Return the active column names in CSV order. */
        const std::vector<std::string>& get_col_names() const{
            static const std::vector<std::string> empty_col_names;
            return (this->col_names) ? this->col_names->get_col_names() : empty_col_names;
        }

        /** Internal accessor for preserving resolved column-name lookup policy across helper types. */
        internals::ConstColNamesPtr col_names_ptr() const noexcept {
            return this->col_names;
        }

        /** Return the index of `col_name`, or `csv::CSV_NOT_FOUND` if absent. */
        int index_of(csv::string_view col_name) const {
            return this->col_names->index_of(col_name);
        }
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

        /** Return speculative-parsing counters for filename-backed readers. */
        internals::SpeculativeParseDiagnostics speculative_diagnostics() const noexcept {
            return this->parser ? this->parser->speculative_diagnostics()
                : internals::SpeculativeParseDiagnostics();
        }

        /** Return the number of parser worker threads used by the active parser. */
        size_t parse_worker_count() const noexcept {
            return this->parser ? this->parser->parse_worker_count() : 1;
        }
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

        /** @name CSV Settings **/
        ///@{
        CSVFormat _format;
        ///@}

        /** @name Parser State */
        ///@{
        /** Pointer to a object containing column information */
        internals::ColNamesPtr col_names = std::make_shared<internals::ColNames>();

        /** Helper class which actually does the parsing */
        std::unique_ptr<internals::parser::CSVParserDriverBase> parser = nullptr;

        /** Queue of parsed CSV rows */
        std::unique_ptr<RowCollection> records{new RowCollection(100)};

        /**
         * Optional owned stream used by two paths:
         *  1) Emscripten filename-constructor fallback to stream parsing
         *  2) Opt-in ownership constructor taking std::unique_ptr<std::istream>
         */
        std::unique_ptr<std::istream> owned_stream = nullptr;

        size_t n_cols = 0;  /**< The number of columns in this CSV */
        size_t _n_rows = 0; /**< How many rows (minus header) have been read so far */

        /** @name Worker Reading Functions
         *
         *  Functions that actively drive parser execution and produce rows.
         */
        ///@{
        bool read_csv(size_t bytes = internals::CSV_CHUNK_SIZE_DEFAULT);
        ///@}

        /**@}*/

    private:
        struct initialize_tag {};
        CSVReader(const CSVFormat& format, initialize_tag)
            : _format(format), read_scheduler_(format.is_threading_enabled()) {}
        CSVError error_;
        void open_file(csv::string_view filename, const CSVFormat& format);

        /** Whether or not rows before header were trimmed */
        bool header_trimmed = false;

        /** How many rows up to and including the header have been trimmed */
        int header_rows_trimmed_ = 0;
        
        /** @name Reader Scheduling: Flags and State */
        ///@{
        size_t _chunk_size = internals::CSV_CHUNK_SIZE_DEFAULT; /**< Current chunk size in bytes */
        bool _read_requested = false; /**< Flag to detect infinite read loops (Issue #218) */
        internals::CSVReadScheduler read_scheduler_;
        ///@}

        void move_state_from(CSVReader& other) noexcept {
            this->_format = std::move(other._format);
            this->error_ = std::move(other.error_);
            this->col_names = std::move(other.col_names);
            this->parser = std::move(other.parser);
            this->records = std::move(other.records);
            this->owned_stream = std::move(other.owned_stream);
            this->n_cols = other.n_cols;
            this->_n_rows = other._n_rows;
            this->header_trimmed = other.header_trimmed;
            this->header_rows_trimmed_ = other.header_rows_trimmed_;
            this->_chunk_size = other._chunk_size;
            this->_read_requested = other._read_requested;
            this->read_scheduler_.set_threading_enabled(this->_format.is_threading_enabled());
            this->read_scheduler_.adopt_exception(other.read_scheduler_.take_exception());
            other.reset_after_move();
        }

        void reset_after_move() noexcept {
            this->n_cols = 0;
            this->_n_rows = 0;
            this->header_trimmed = false;
            this->header_rows_trimmed_ = 0;
            this->_read_requested = false;
            this->_chunk_size = internals::CSV_CHUNK_SIZE_DEFAULT;
        }

        /** @name Format and Header Helpers
         *
         *  Parser bootstrap and metadata/header handling shared across mmap and stream paths.
         */
        ///@{
        /** Shared parser installation after source-specific bootstrap has completed
         *  in concrete parser implementations.
         */
        void init_parser(std::unique_ptr<internals::parser::CSVParserDriverBase> parser);

        template<typename TStream,
            csv::enable_if_t<std::is_base_of<std::istream, TStream>::value, int> = 0>
        void init_from_stream(TStream& source, const CSVFormat& format) {
            this->init_parser(
                std::unique_ptr<internals::parser::CSVParserDriverBase>(
                    new internals::parser::StreamParser<TStream>(source, format, this->col_names)
                )
            );
        }

        /** Read until the header row is consumed (or EOF) to get metadata */
        void initial_read();

        void trim_header();
        ///@}

        /** @name Reading Helpers
         *
         *  Shared consumer-side logic used by both single-row and batch reads.
         */
        ///@{
        /** Apply variable-column policy to a parsed row and emit it if accepted. */
        bool accept_row(CSVRow&& candidate, CSVRow* single_row, std::vector<CSVRow>* batch_rows);

        /**
         * Try to pull more rows from the queue, returning false if we're done.
         * 
         * Returns true if parsing is still parsing, or the queue has hot and fresh
         * rows ready.
         */
        bool check_for_rows();

        /** Drain as many already-queued rows as possible into a caller-owned chunk buffer.
         *
         *  Applies the same variable-column filtering policy as read_row(), but amortizes
         *  queue synchronization across many rows.
         */
        void drain_rows_into_chunk(std::vector<CSVRow>& out, size_t max_rows);
        ///@}
    };

    #if defined(CSV_HAS_CXX20) && CSV_EXCEPTIONS_ENABLED
    static_assert(
        internals::csv_write_rows_input_range<CSVReader>,
        "CSVReader must remain compatible with csv::DelimWriter::write_rows()."
    );
    #endif
}
