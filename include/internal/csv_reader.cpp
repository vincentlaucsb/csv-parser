/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#include "csv_reader.hpp"

namespace csv {
    /** Reads an arbitrarily large CSV file using memory-mapped IO.
     *
     *  **Details:** Reads the first block of a CSV file synchronously to get information
     *               such as column names and delimiting character.
     *
     *  \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
	CSV_INLINE CSVReader::CSVReader(csv::string_view filename, CSVFormat format) : _format(format) {
#if defined(__EMSCRIPTEN__)
        this->owned_stream = std::unique_ptr<std::istream>(
            new std::ifstream(std::string(filename), std::ios::binary)
        );

        if (!(*this->owned_stream)) {
            throw std::runtime_error("Cannot open file " + std::string(filename));
        }

        this->init_from_stream(*this->owned_stream, format);
#else
        this->init_parser(std::unique_ptr<internals::IBasicCSVParser>(
            new internals::MmapParser(filename, format, this->col_names)
        ));
#endif
    }

    CSV_INLINE void CSVReader::init_parser(
        std::unique_ptr<internals::IBasicCSVParser> parser
    ) {
        auto resolved = parser->get_resolved_format();
        this->_format = resolved.format;
        this->_chunk_size = this->_format.get_chunk_size();
        this->n_cols = resolved.n_cols;

        if (!this->_format.col_names.empty()) {
            this->set_col_names(this->_format.col_names);
        }

        this->parser = std::move(parser);
        this->initial_read();
    }

    /** Return the format of the original raw CSV */
    CSV_INLINE CSVFormat CSVReader::get_format() const {
        CSVFormat new_format = this->_format;

        // Since users are normally not allowed to set
        // column names and header row simulatenously,
        // we will set the backing variables directly here
        new_format.col_names = this->col_names->get_col_names();
        new_format.header = this->_format.header;

        return new_format;
    }

    /** Return the CSV's column names as a vector of strings. */
    CSV_INLINE std::vector<std::string> CSVReader::get_col_names() const {
        return (this->col_names) ? this->col_names->get_col_names() : 
            std::vector<std::string>();
    }

    /** Return the index of the column name if found or
     *         csv::CSV_NOT_FOUND otherwise.
     */
    CSV_INLINE int CSVReader::index_of(csv::string_view col_name) const {
        return this->col_names->index_of(col_name);
    }

    CSV_INLINE void CSVReader::trim_header() {
        if (!this->header_trimmed) {
            for (int i = 0; i <= this->_format.header && !this->records->empty(); i++) {
                if (i == this->_format.header && this->col_names->empty()) {
                    this->set_col_names(this->records->pop_front());
                }
                else {
                    this->records->pop_front();
                }
            }

            this->header_trimmed = true;
        }
    }

    /** Install the active column names for this reader. */
    CSV_INLINE void CSVReader::set_col_names(const std::vector<std::string>& names)
    {
        this->col_names->set_policy(this->_format.get_column_name_policy());
        this->col_names->set_col_names(names);
        this->n_cols = names.size();
    }

    /**
     * Read a chunk of CSV data.
     *
     * @note This method is meant to be run on its own thread. Only one `read_csv()` thread
     *       should be active at a time.
     *
     * @see CSVReader::read_csv_worker
     * @see CSVReader::read_row()
     */
    CSV_INLINE bool CSVReader::read_csv(size_t bytes) {
        // WORKER THREAD FUNCTION: Runs asynchronously to read CSV chunks
        //
        // Threading model:
        // 1. notify_all() - signals read_row() that worker is active
        // 2. parser->next() - reads and parses bytes (10MB chunks)
        // 3. kill_all() - signals read_row() that worker is done
        //
        // Exception handling: Exceptions thrown here MUST propagate to the calling
        // thread via std::exception_ptr. Bug #282 fixed cases where exceptions were
        // swallowed, causing std::terminate() instead of proper error handling.
        
        // Tell read_row() to listen for CSV rows
        this->records->notify_all();

        try {
            this->parser->set_output(*this->records);
            this->parser->next(bytes);

            if (!this->header_trimmed) {
                this->trim_header();
            }
        }
        catch (...) {
            // Never allow exceptions to escape the worker thread, or std::terminate will be invoked.
            // Store the exception and rethrow from the consumer thread (read_row / iterator).
            this->set_read_csv_exception(std::current_exception());
        }

        // Tell read_row() to stop waiting
        this->records->kill_all();

        return true;
    }

    /**
     * Retrieve rows as CSVRow objects, returning true if more rows are available.
     *
     * @par Performance Notes
    *  - Reads chunks of data that are csv::internals::CSV_CHUNK_SIZE_DEFAULT bytes large at a time
     *  - For performance details, read the documentation for CSVRow and CSVField.
     *
     * @see CSVRow, CSVField
     *
     * **Example:**
     * \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
    CSV_INLINE bool CSVReader::read_row(CSVRow &row) {
        while (true) {
            if (this->records->empty()) {
#if CSV_ENABLE_THREADS
                if (this->records->is_waitable()) {
                    // Reading thread is currently active => wait for it to populate records
                    this->records->wait();
                    continue;
                }
#endif

                // Reading thread is not active
                JOIN_WORKER(this->read_csv_worker);

                // If the worker thread failed, rethrow the error here
                this->rethrow_read_csv_exception_if_any();

                if (this->parser->eof())
                    // End of file and no more records
                    return false;

                // Detect infinite loop: a previous read was requested but records are still empty.
                // This fires when a single row spans more than 2 × _chunk_size bytes:
                //   - chunk N   fills without finding '\n'  → _read_requested set to true
                //   - chunk N+1 also fills without '\n'     → guard fires here
                // Default _chunk_size is CSV_CHUNK_SIZE_DEFAULT (10 MB), so the threshold is
                // rows > 20 MB.  Use CSVFormat::chunk_size() to raise the limit.
                if (this->_read_requested && this->records->empty()) {
                    throw std::runtime_error(
                        "End of file not reached and no more records parsed. "
                        "This likely indicates a CSV row larger than the chunk size of " +
                        std::to_string(this->_chunk_size) + " bytes. "
                        "Use CSVFormat::chunk_size() to increase the chunk size."
                    );
                }

#if CSV_ENABLE_THREADS
                // Start another reading thread.
                // Mark as waitable before starting the thread to avoid a race where
                // read_row() observes is_waitable()==false immediately after thread creation.
                this->records->notify_all();
                this->read_csv_worker = std::thread(&CSVReader::read_csv, this, this->_chunk_size);
#else
                // Single-threaded mode parses synchronously on the caller thread.
                this->read_csv(this->_chunk_size);
                this->rethrow_read_csv_exception_if_any();
#endif
                this->_read_requested = true;
                continue;
            }
            else {
                const auto policy = this->_format.variable_column_policy;
                const size_t next_row_size = this->records->front().size();

                if (policy == VariableColumnPolicy::KEEP_NON_EMPTY && next_row_size == 0) {
                    this->records->pop_front();
                    continue;
                }

                if (next_row_size != this->n_cols &&
                    (policy == VariableColumnPolicy::THROW || policy == VariableColumnPolicy::IGNORE_ROW)) {
                    auto errored_row = this->records->pop_front();

                    if (policy == VariableColumnPolicy::THROW) {
                        if (errored_row.size() < this->n_cols)
                            throw std::runtime_error("Line too short " + std::string(errored_row.raw_str()));

                        throw std::runtime_error("Line too long " + std::string(errored_row.raw_str()));
                    }

                    continue;
                }

                row = this->records->pop_front();
                this->_n_rows++;
                this->_read_requested = false;  // Reset flag on successful read
                return true;
            }
        }

        return false;
    }
}
