#include "csv_parser.hpp"

namespace csv {
    CSVReader::iterator CSVReader::begin() {
        // Rewind the file pointer back
        if (!this->first_read) {
            std::rewind(this->infile);
            this->records.clear();
            this->quote_escape = false;
            this->c_pos = 0;
            this->n_pos = 0;
            this->read_csv("", ITERATION_CHUNK_SIZE, false);
        }

        CSVReader::iterator ret(this, std::move(this->records.front()));
        this->records.pop_front();
        return ret;
    }

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

    CSVReader::iterator::reference CSVReader::iterator::operator*() {
        return this->row;
    }

    CSVReader::iterator::pointer CSVReader::iterator::operator->() {
        return &(this->row);
    }

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

    bool CSVReader::iterator::operator==(const CSVReader::iterator& other) const {
        return (this->daddy == other.daddy) && (this->i == other.i);
    }
}