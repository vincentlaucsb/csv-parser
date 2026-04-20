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

#if !defined(CSV_ENABLE_THREADS) || CSV_ENABLE_THREADS
#include <mutex>
#include <thread>
#endif

#include "basic_csv_parser.hpp"
#include "common.hpp"
#include "data_type.hpp"
#include "csv_format.hpp"

/** The all encompassing namespace */
namespace csv {
#if CSV_ENABLE_THREADS
    inline void join_worker(std::thread& worker) {
        if (worker.joinable()) worker.join();
    }

    #define JOIN_WORKER(worker) join_worker(worker)
#else
    #define JOIN_WORKER(worker) ((void)0)
#endif

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
        CSVReader(csv::string_view filename, CSVFormat format = CSVFormat::guess_csv()) : _format(format) {
#if defined(__EMSCRIPTEN__)
            this->owned_stream = std::unique_ptr<std::istream>(
                new std::ifstream(std::string(filename), std::ios::binary)
            );

            if (!(*this->owned_stream)) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            this->init_from_stream(*this->owned_stream, format);
#else
            // C4316: MmapParser may carry over-aligned SIMD members. Allocation
            // alignment is handled by the allocator on supported platforms;
            // suppress MSVC's false-positive warning at this site.
            CSV_MSVC_PUSH_DISABLE(4316)
            this->init_parser(std::unique_ptr<internals::IBasicCSVParser>(
                new internals::MmapParser(filename, format, this->col_names)
            ));
            CSV_MSVC_POP
#endif
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
        CSVReader(TStream &source, CSVFormat format = CSVFormat::guess_csv()) : _format(format) {
            this->init_from_stream(source, format);
        }

        /** @brief Construct CSVReader from an owned std::istream
         *
         *  This is an opt-in safety switch for stream lifetime management.
         *  CSVReader takes ownership and guarantees the stream outlives parsing.
         */
        CSVReader(std::unique_ptr<std::istream> source,
            const CSVFormat& format = CSVFormat::guess_csv()) : _format(format), owned_stream(std::move(source)) {
            if (!this->owned_stream) {
                throw std::invalid_argument("CSVReader requires a non-null stream");
            }

            this->init_from_stream(*this->owned_stream, format);
        }
        ///@}

        CSVReader(const CSVReader&) = delete;             ///< Not copyable
        CSVReader& operator=(const CSVReader&) = delete;  ///< Not copyable

        /** Move constructor.
         *
         * Required so C++11 builds can return CSVReader by value from helpers like
         * csv::parse()/csv::parse_unsafe(), where copy elision is not guaranteed.
         *
         * Any active worker on the source is joined before moving parser state to
         * avoid a thread continuing to run against the source object's address.
         */
        CSVReader(CSVReader&& other) noexcept :
            _format(std::move(other._format)),
            col_names(std::move(other.col_names)),
            parser(std::move(other.parser)),
            records(std::move(other.records)),
            owned_stream(std::move(other.owned_stream)),
            n_cols(other.n_cols),
            _n_rows(other._n_rows),
            header_trimmed(other.header_trimmed),
            _chunk_size(other._chunk_size),
            _read_requested(other._read_requested),
            read_csv_exception(other.take_read_csv_exception()) {
            JOIN_WORKER(other.read_csv_worker);

            other.n_cols = 0;
            other._n_rows = 0;
            other.header_trimmed = false;
            other._read_requested = false;
            other._chunk_size = internals::CSV_CHUNK_SIZE_DEFAULT;
        }

        /** Move assignment.
         *
         * Joins active workers on both sides before transferring parser state.
         */
        CSVReader& operator=(CSVReader&& other) noexcept {
            if (this == &other) {
                return *this;
            }

            JOIN_WORKER(this->read_csv_worker);
            JOIN_WORKER(other.read_csv_worker);

            this->_format = std::move(other._format);
            this->col_names = std::move(other.col_names);
            this->parser = std::move(other.parser);
            this->records = std::move(other.records);
            this->owned_stream = std::move(other.owned_stream);
            this->n_cols = other.n_cols;
            this->_n_rows = other._n_rows;
            this->header_trimmed = other.header_trimmed;
            this->_chunk_size = other._chunk_size;
            this->_read_requested = other._read_requested;
            this->read_csv_exception = other.take_read_csv_exception();

            other.n_cols = 0;
            other._n_rows = 0;
            other.header_trimmed = false;
            other._read_requested = false;
            other._chunk_size = internals::CSV_CHUNK_SIZE_DEFAULT;

            return *this;
        }

        ~CSVReader() {
            JOIN_WORKER(this->read_csv_worker);
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

        /**
         * Optional owned stream used by two paths:
         *  1) Emscripten filename-constructor fallback to stream parsing
         *  2) Opt-in ownership constructor taking std::unique_ptr<std::istream>
         */
        std::unique_ptr<std::istream> owned_stream = nullptr;

        size_t n_cols = 0;  /**< The number of columns in this CSV */
        size_t _n_rows = 0; /**< How many rows (minus header) have been read so far */

        /** @name Multi-Threaded File Reading Functions */
        ///@{
        bool read_csv(size_t bytes = internals::CSV_CHUNK_SIZE_DEFAULT);
        ///@}

        /**@}*/

    private:
        /** Whether or not rows before header were trimmed */
        bool header_trimmed = false;
        /** @name Multi-Threaded File Reading: Flags and State */
        ///@{
    #if CSV_ENABLE_THREADS
        std::thread read_csv_worker; /**< Worker thread for read_csv() */
    #endif
        size_t _chunk_size = internals::CSV_CHUNK_SIZE_DEFAULT; /**< Current chunk size in bytes */
        bool _read_requested = false; /**< Flag to detect infinite read loops (Issue #218) */
        ///@}

        /** If the worker thread throws, store it here and rethrow on the consumer thread. */
        std::exception_ptr read_csv_exception = nullptr;
#if CSV_ENABLE_THREADS
        std::mutex read_csv_exception_lock;
#endif

        void set_read_csv_exception(std::exception_ptr eptr) {
#if CSV_ENABLE_THREADS
            std::lock_guard<std::mutex> lock(this->read_csv_exception_lock);
#endif
            this->read_csv_exception = std::move(eptr);
        }

        std::exception_ptr take_read_csv_exception() {
#if CSV_ENABLE_THREADS
            std::lock_guard<std::mutex> lock(this->read_csv_exception_lock);
#endif
            auto eptr = this->read_csv_exception;
            this->read_csv_exception = nullptr;
            return eptr;
        }

        void rethrow_read_csv_exception_if_any() {
            if (auto eptr = this->take_read_csv_exception()) {
                std::rethrow_exception(eptr);
            }
        }

        /** Shared parser installation after source-specific bootstrap has completed
         *  in concrete parser implementations.
         */
        void init_parser(std::unique_ptr<internals::IBasicCSVParser> parser);

        template<typename TStream,
            csv::enable_if_t<std::is_base_of<std::istream, TStream>::value, int> = 0>
        void init_from_stream(TStream& source, CSVFormat format) {
            // C4316: StreamParser may have over-aligned SIMD members; heap allocation
            // alignment is handled correctly at runtime via the allocator on supported
            // platforms. Suppress the MSVC false-positive here.
            CSV_MSVC_PUSH_DISABLE(4316)
            this->init_parser(
                std::unique_ptr<internals::IBasicCSVParser>(
                    new internals::StreamParser<TStream>(source, format, this->col_names)
                )
            );
            CSV_MSVC_POP
        }

        /** Read initial chunk to get metadata */
        void initial_read() {
#if CSV_ENABLE_THREADS
            this->read_csv_worker = std::thread(&CSVReader::read_csv, this, this->_chunk_size);
            this->read_csv_worker.join();
#else
            this->read_csv(this->_chunk_size);
#endif
            this->rethrow_read_csv_exception_if_any();
        }

        void trim_header();
    };
}
