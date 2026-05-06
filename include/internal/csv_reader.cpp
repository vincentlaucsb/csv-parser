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

        if (this->read_scheduler_.wait_if_active(
            [this] { return this->records->is_waitable(); },
            [this] { this->records->wait(); }
        )) {
            return true;
        }

        this->read_scheduler_.join();
        this->read_scheduler_.rethrow_exception_if_any();

        if (this->parser->eof()) return false;

        if (this->_read_requested && this->records->empty()) {
            internals::throw_row_too_large_for_chunk(this->_chunk_size);
        }

        this->read_scheduler_.run(
            [this] { this->read_csv(this->_chunk_size); },
            [this] { this->records->notify_all(); }
        );
        this->read_scheduler_.rethrow_exception_if_any();
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
        std::unique_ptr<internals::parser::CSVParserDriverBase> parser_impl
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
                    internals::throw_line_too_short(candidate.raw_str());
                }

                internals::throw_line_too_long(candidate.raw_str());
            }

            return false;
        }

        if (single_row != nullptr) {
            *single_row = std::move(candidate);
        } else if (batch_rows != nullptr) {
            batch_rows->push_back(std::move(candidate));
        } else {
            return false;
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
     * @note This method may run on a worker thread or synchronously on the caller
     *       thread when CSVFormat::threading(false) is active. Only one read_csv()
     *       invocation should be active at a time.
     *
     * @see csv::internals::CSVReadScheduler
     * @see CSVReader::read_row()
     */
    CSV_INLINE bool CSVReader::read_csv(size_t bytes) {
        // SCHEDULED READ FUNCTION: Runs asynchronously when runtime threading
        // is enabled, or synchronously when CSVFormat::threading(false) is active.
        //
        // Threading model:
        // 1. notify_all() - signals read_row() that worker is active
        // 2. parser->next() - reads and parses bytes (10MB chunks)
        // 3. kill_all() - signals read_row() that worker is done
        //
        // Exception handling: CSVReadScheduler catches exceptions and rethrows
        // them on the consumer thread. Bug #282 fixed cases where worker
        // exceptions were swallowed, causing std::terminate().
        
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
            this->records->kill_all();
            throw;
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
