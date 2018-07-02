#include "csv_parser.hpp"

namespace csv {
    /**
     * @brief Return an iterator to the first row in the reader
     *
     */
    CSVReader::iterator CSVReader::begin() {
        CSVReader::iterator ret(this, std::move(this->records.front()));
        this->records.pop_front();
        return ret;
    }

    /**
     * @brief A placeholder for the imaginary past the end row in a CSV.
     *        Attempting to deference this will lead to bad things.
     */
    CSVReader::iterator CSVReader::end() {
        return CSVReader::iterator();
    }

    /////////////////////////
    // CSVReader::iterator //
    /////////////////////////

    CSVReader::iterator::iterator(CSVReader* _daddy, CSVRow&& _row) :
        daddy(_daddy) {
        row = std::move(_row);
    }

    /** @brief Access the CSVRow held by the iterator */
    CSVReader::iterator::reference CSVReader::iterator::operator*() {
        return this->row;
    }

    /** @brief Return a pointer to the CSVRow the iterator has stopped at */
    CSVReader::iterator::pointer CSVReader::iterator::operator->() {
        return &(this->row);
    }

    /** @brief Advance the iterator by one row. If this CSVReader has an
     *  associated file, then the iterator will lazily pull more data from
     *  that file until EOF.
     */
    CSVReader::iterator& CSVReader::iterator::operator++() {
        if (daddy->records.empty()) {
            if (!daddy->eof())
                daddy->read_csv("", ITERATION_CHUNK_SIZE, false);
            else {
                this->daddy = nullptr; // this == end()
                return *this;
            }
        }

        this->row  = std::move(daddy->records.front());
        this->daddy->records.pop_front();
        return *this;
    }

    CSVReader::iterator CSVReader::iterator::operator++(int) {
        auto temp = *this;
        if (daddy->records.empty()) {
            if (!daddy->eof())
                daddy->read_csv("", ITERATION_CHUNK_SIZE, false);
            else {
                this->daddy = nullptr;
                return temp;
            }
        }

        this->row = std::move(daddy->records.front());
        this->daddy->records.pop_front();
        return temp;
    }

    /** @brief Returns true if iterators were constructed from the same CSVReader
     *         and point to the same row
     */
    bool CSVReader::iterator::operator==(const CSVReader::iterator& other) const {
        return (this->daddy == other.daddy) && (this->i == other.i);
    }
}