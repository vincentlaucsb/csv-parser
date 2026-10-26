/** @file
 *  @brief Defines functionality needed for basic CSV parsing
 */

#include "csv_reader.hpp"

namespace csv {
    void CSVReader::open_file(csv::string_view filename, const CSVFormat& format) {
#if defined(__EMSCRIPTEN__)
        owned_stream.reset(new std::ifstream(std::string(filename), std::ios::binary));
        if (!*owned_stream) {
            internals::fail(error_, CSVErrorCode::OpenFailed,
                internals::make_prefixed_message(internals::ERROR_CANNOT_OPEN_FILE, filename));
            return;
        }
        init_from_stream(*owned_stream, format);
#else
        init_parser(std::unique_ptr<internals::parser::CSVParserDriverBase>(
            new internals::parser::MmapParser(filename, format, col_names)));
#endif
    }

    std::unique_ptr<CSVReader> CSVReader::try_open(
        csv::string_view filename, CSVError& error, const CSVFormat& format) {
        error.clear();
        std::unique_ptr<CSVReader> reader(new CSVReader(format, initialize_tag{}));
        reader->open_file(filename, format);
        error = reader->error_;
        return error ? nullptr : std::move(reader);
    }

    std::unique_ptr<CSVReader> CSVReader::try_open(
        std::unique_ptr<std::istream> source, CSVError& error, const CSVFormat& format) {
        error.clear();
        if (!source) {
            internals::fail_static(error, CSVErrorCode::InvalidArgument, internals::ERROR_READER_NULL_STREAM);
            return nullptr;
        }
        std::unique_ptr<CSVReader> reader(new CSVReader(format, initialize_tag{}));
        reader->owned_stream = std::move(source);
        reader->init_from_stream(*reader->owned_stream, format);
        error = reader->error_;
        return error ? nullptr : std::move(reader);
    }

#ifdef _MSC_VER
#pragma region Reading helpers
#endif
    bool CSVReader::check_for_rows() {
        if (error_) return false;
        if (!this->records->empty()) return true;

        if (this->read_scheduler_.wait_if_active(
            [this] { return this->records->is_waitable(); },
            [this] { this->records->wait(); }
        )) {
            return true;
        }

        this->read_scheduler_.join();
        this->read_scheduler_.rethrow_exception_if_any();
        if (parser->error()) {
            error_ = parser->error();
            return false;
        }

        if (this->parser->eof()) return false;

        if (this->_read_requested && this->records->empty()) {
            return internals::fail(error_, CSVErrorCode::RowTooLarge,
                internals::make_row_larger_than_chunk_message(this->_chunk_size));
        }

        this->read_scheduler_.run(
            [this] { this->read_csv(this->_chunk_size); },
            [this] { this->records->notify_all(); }
        );
        this->read_scheduler_.rethrow_exception_if_any();
        // The parser may still be running. Inspect its error only after join().
        this->_read_requested = true;
        return true;
    }

#ifdef _MSC_VER
#pragma endregion Reading helpers
#endif

#ifdef _MSC_VER
#pragma region Format and header helpers
#endif
    void CSVReader::init_parser(
        std::unique_ptr<internals::parser::CSVParserDriverBase> parser_impl
    ) {
        if (parser_impl->error()) {
            error_ = parser_impl->error();
            return;
        }
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

    CSVFormat CSVReader::get_format() const {
        CSVFormat new_format = this->_format;

        // Since users are normally not allowed to set
        // column names and header row simulatenously,
        // we will set the backing variables directly here
        new_format.col_names = this->col_names->get_col_names();
        new_format.header = this->_format.header;

        return new_format;
    }

    void CSVReader::initial_read() {
        // Issue #337: A single read window may end before the header row when
        // earlier rows are long, so keep reading until the header is consumed.
        do {
            const int trimmed_before = this->header_rows_trimmed_;
            this->read_scheduler_.run([this] { this->read_csv(this->_chunk_size); });
            this->read_scheduler_.join();
            this->read_scheduler_.rethrow_exception_if_any();
            if (parser->error()) {
                error_ = parser->error();
                return;
            }

            // trim_header() drains the queue until the header is consumed, so an
            // untrimmed header with no progress means the read produced no rows.
            if (!this->header_trimmed && !this->parser->eof()
                && this->header_rows_trimmed_ == trimmed_before) {
                internals::fail(error_, CSVErrorCode::RowTooLarge,
                    internals::make_row_larger_than_chunk_message(this->_chunk_size));
                return;
            }
        } while (!this->header_trimmed && !this->parser->eof());
    }

    void CSVReader::trim_header() {
        if (!this->header_trimmed) {
            // The rows up to and including the header may arrive over several
            // reads, so track progress instead of assuming one read holds them all.
            while (this->header_rows_trimmed_ <= this->_format.header && !this->records->empty()) {
                if (this->header_rows_trimmed_ == this->_format.header && this->col_names->empty()) {
                    this->set_col_names(this->records->pop_front());
                }
                else {
                    this->records->pop_front();
                }

                this->header_rows_trimmed_++;
            }

            this->header_trimmed = this->header_rows_trimmed_ > this->_format.header;
        }
    }

    /** Install the active column names for this reader. */
    void CSVReader::set_col_names(const std::vector<std::string>& names)
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
    bool CSVReader::accept_row(CSVRow&& candidate, CSVRow* single_row, std::vector<CSVRow>* batch_rows) {
        const auto policy = this->_format.variable_column_policy;
        const size_t next_row_size = candidate.size();

        if (policy == VariableColumnPolicy::KEEP_NON_EMPTY && next_row_size == 0) {
            return false;
        }

        if (next_row_size != this->n_cols &&
            (policy == VariableColumnPolicy::THROW || policy == VariableColumnPolicy::IGNORE_ROW)) {
            if (policy == VariableColumnPolicy::THROW) {
                if (candidate.size() < this->n_cols) {
                    return internals::fail(error_, CSVErrorCode::RowLengthMismatch,
                        internals::make_prefixed_message(internals::ERROR_ROW_TOO_SHORT, candidate.raw_str()));
                }

                return internals::fail(error_, CSVErrorCode::RowLengthMismatch,
                    internals::make_prefixed_message(internals::ERROR_ROW_TOO_LONG, candidate.raw_str()));
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

    void CSVReader::drain_rows_into_chunk(std::vector<CSVRow>& out, size_t max_rows) {
        std::vector<CSVRow> drained;
        drained.reserve(max_rows - out.size());
        this->records->drain_front(drained, max_rows - out.size());

        for (size_t i = 0; i < drained.size(); ++i) {
            this->accept_row(std::move(drained[i]), nullptr, &out);
            if (error_) break;
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
    bool CSVReader::read_csv(size_t bytes) {
        // SCHEDULED READ FUNCTION: Runs asynchronously when runtime threading
        // is enabled, or synchronously when CSVFormat::threading(false) is active.
        //
        // Threading model:
        // 1. notify_all() - signals read_row() that worker is active
        // 2. parser->next() - reads and parses bytes (10MB chunks)
        // 3. kill_all() - signals read_row() that worker is done
        //
        // Operational errors stay on the source adapter until the consumer joins.
        // In exception-enabled builds, CSVReadScheduler catches exceptions and rethrows
        // them on the consumer thread. Bug #282 fixed cases where worker
        // exceptions were swallowed, causing std::terminate().
        
        // Tell read_row() to listen for CSV rows
        this->records->notify_all();

        CSV_TRY {
            this->parser->set_output(*this->records);
            this->parser->next(bytes);

            if (!parser->error() && !this->header_trimmed) {
                this->trim_header();
            }
        }
        CSV_CATCH((...), {
            this->records->kill_all();
            throw;
        })

        // Tell read_row() to stop waiting
        this->records->kill_all();

        return true;
    }

    bool CSVReader::try_read_row(CSVRow& row, CSVError& error) {
        error.clear();
        while (this->check_for_rows()) {
            if (this->records->empty())
                continue;
                
            if (this->accept_row(this->records->pop_front(), &row, nullptr))
                return true;
        }

        error = error_;
        return false;
    }

    bool CSVReader::try_read_chunk(std::vector<CSVRow>& out, size_t max_rows, CSVError& error) {
        error.clear();
        out.clear();

        if (max_rows == 0) {
            return internals::fail_static(error, CSVErrorCode::InvalidArgument, "read_chunk() requires a non-zero row count.");
        }

        while (out.size() < max_rows) {
            if (check_for_rows()) {
                if (this->records->empty()) {
                    continue;
                }

                const size_t before_size = out.size();
                this->drain_rows_into_chunk(out, max_rows);
                if (error_) { error = error_; return false; }

                if (out.size() == before_size) {
                    continue;
                }
            }
            else { error = error_; return !error && !out.empty(); }
        }

        return true;
    }
#if CSV_EXCEPTIONS_ENABLED
    bool CSVReader::read_row(CSVRow& row) {
        CSVError error;
        const bool read = try_read_row(row, error);
        internals::throw_error(error);
        return read;
    }

    bool CSVReader::read_chunk(std::vector<CSVRow>& out, size_t max_rows) {
        if (max_rows == 0) { out.clear(); return false; }
        CSVError error;
        const bool read = try_read_chunk(out, max_rows, error);
        internals::throw_error(error);
        return read;
    }
#endif
#ifdef _MSC_VER
#pragma endregion Worker reading methods
#endif
}
