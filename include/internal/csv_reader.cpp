/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#include "csv_reader.hpp"

namespace csv {
#ifdef _MSC_VER
#pragma region Reading helpers
#endif
    CSV_INLINE bool CSVReader::check_for_rows() {
        if (!this->records->empty()) return true;

#if CSV_ENABLE_THREADS
        if (this->records->is_waitable()) {
            this->records->wait();
            return true;
        }
#endif

        JOIN_WORKER(this->read_csv_worker);
        this->rethrow_read_csv_exception_if_any();

        if (this->parser->eof()) return false;

        if (this->_read_requested && this->records->empty()) {
            throw std::runtime_error(
                "End of file not reached and no more records parsed. "
                "This likely indicates a CSV row larger than the chunk size of " +
                std::to_string(this->_chunk_size) + " bytes. "
                "Use CSVFormat::chunk_size() to increase the chunk size."
            );
        }

#if CSV_ENABLE_THREADS
        this->records->notify_all();
        this->read_csv_worker = std::thread(&CSVReader::read_csv, this, this->_chunk_size);
#else
        this->read_csv(this->_chunk_size);
        this->rethrow_read_csv_exception_if_any();
#endif
        this->_read_requested = true;
        return true;
    }

#ifdef _MSC_VER
#pragma endregion Reading helpers
#endif

#ifdef _MSC_VER
#pragma region Format and header helpers
#endif
    CSV_INLINE void CSVReader::init_parser(
        std::unique_ptr<internals::IBasicCSVParser> parser_impl
    ) {
        auto resolved = parser_impl->get_resolved_format();
        this->_format = resolved.format;
        this->_chunk_size = this->_format.get_chunk_size();
        this->n_cols = resolved.n_cols;

        if (!this->_format.col_names.empty()) {
            this->set_col_names(this->_format.col_names);
        }

        this->parser = std::move(parser_impl);
        this->initial_read();
    }

    CSV_INLINE CSVFormat CSVReader::get_format() const {
        CSVFormat new_format = this->_format;

        // Since users are normally not allowed to set
        // column names and header row simulatenously,
        // we will set the backing variables directly here
        new_format.col_names = this->col_names->get_col_names();
        new_format.header = this->_format.header;

        return new_format;
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
#ifdef _MSC_VER
#pragma endregion Format and header helpers
#endif

#ifdef _MSC_VER
#pragma region Reading helpers
#endif
    CSV_INLINE bool CSVReader::accept_row(CSVRow&& candidate, CSVRow* single_row, std::vector<CSVRow>* batch_rows) {
        const auto policy = this->_format.variable_column_policy;
        const size_t next_row_size = candidate.size();

        if (policy == VariableColumnPolicy::KEEP_NON_EMPTY && next_row_size == 0) {
            return false;
        }

        if (next_row_size != this->n_cols &&
            (policy == VariableColumnPolicy::THROW || policy == VariableColumnPolicy::IGNORE_ROW)) {
            if (policy == VariableColumnPolicy::THROW) {
                if (candidate.size() < this->n_cols) {
                    throw std::runtime_error("Line too short " + std::string(candidate.raw_str()));
                }

                throw std::runtime_error("Line too long " + std::string(candidate.raw_str()));
            }

            return false;
        }

        if (single_row != nullptr) {
            *single_row = std::move(candidate);
        } else {
            batch_rows->push_back(std::move(candidate));
        }

        this->_n_rows++;
        this->_read_requested = false;
        return true;
    }

    CSV_INLINE void CSVReader::drain_rows_into_chunk(std::vector<CSVRow>& out, size_t max_rows) {
        std::vector<CSVRow> drained;
        drained.reserve(max_rows - out.size());
        this->records->drain_front(drained, max_rows - out.size());

        for (size_t i = 0; i < drained.size(); ++i) {
            this->accept_row(std::move(drained[i]), nullptr, &out);
        }
    }
#ifdef _MSC_VER
#pragma endregion Reading helpers
#endif

#ifdef _MSC_VER
#pragma region Worker reading methods
#endif

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

    CSV_INLINE bool CSVReader::read_row(CSVRow &row) {
        while (this->check_for_rows()) {
            if (this->records->empty())
                continue;
                
            if (this->accept_row(this->records->pop_front(), &row, nullptr))
                return true;
        }

        return false;
    }

    CSV_INLINE bool CSVReader::read_chunk(std::vector<CSVRow>& out, size_t max_rows) {
        out.clear();

        if (max_rows == 0) {
            return false;
        }

        while (out.size() < max_rows) {
            if (check_for_rows()) {
                if (this->records->empty()) {
                    continue;
                }

                const size_t before_size = out.size();
                this->drain_rows_into_chunk(out, max_rows);

                if (out.size() == before_size) {
                    continue;
                }
            }
            else return !out.empty();
        }

        return true;
    }
#ifdef _MSC_VER
#pragma endregion Worker reading methods
#endif
}
