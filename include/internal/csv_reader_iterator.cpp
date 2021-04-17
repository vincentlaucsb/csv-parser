/** @file
 *  Defines an input iterator for csv::CSVReader
 */

#include "csv_reader.hpp"

namespace csv {
    /** Return an iterator to the first row in the reader */
    CSV_INLINE CSVReader::iterator CSVReader::begin() {
        if (this->records->empty()) {
            this->read_csv_worker = std::thread(&CSVReader::read_csv, this, internals::ITERATION_CHUNK_SIZE);
            this->read_csv_worker.join();

            // Still empty => return end iterator
            if (this->records->empty()) return this->end();
        }

        CSVReader::iterator ret(this, this->records->pop_front());
        return ret;
    }

    /** A placeholder for the imaginary past the end row in a CSV.
     *  Attempting to deference this will lead to bad things.
     */
    CSV_INLINE HEDLEY_CONST CSVReader::iterator CSVReader::end() const noexcept {
        return CSVReader::iterator();
    }

    /////////////////////////
    // CSVReader::iterator //
    /////////////////////////

    CSV_INLINE CSVReader::iterator::iterator(CSVReader* _daddy, CSVRow&& _row) :
        daddy(_daddy) {
        row = std::move(_row);
    }

    /** Advance the iterator by one row. If this CSVReader has an
     *  associated file, then the iterator will lazily pull more data from
     *  that file until the end of file is reached.
     *
     *  @note This iterator does **not** block the thread responsible for parsing CSV.
     *
     */
    CSV_INLINE CSVReader::iterator& CSVReader::iterator::operator++() {
        if (!daddy->read_row(this->row)) {
            this->daddy = nullptr; // this == end()
        }

        return *this;
    }

    /** Post-increment iterator */
    CSV_INLINE CSVReader::iterator CSVReader::iterator::operator++(int) {
        auto temp = *this;
        if (!daddy->read_row(this->row)) {
            this->daddy = nullptr; // this == end()
        }

        return temp;
    }
}
