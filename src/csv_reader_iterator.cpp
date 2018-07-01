#include "csv_parser.hpp"

namespace csv {
    CSVReader::iterator::iterator(CSVReader* _daddy, CSVRow&& _row) :
        daddy(_daddy) {
        row = std::make_shared<CSVRow>(std::move(_row));
    }

    CSVReader::iterator::reference CSVReader::iterator::operator*() const {
        return *(this->row.get());
    }

    CSVReader::iterator::pointer CSVReader::iterator::operator->() const {
        return this->row.get();
    }

    CSVReader::iterator& CSVReader::iterator::operator++() {
        if (daddy->records.empty()) {
            if (!daddy->eof())
                daddy->read_csv("", ITERATION_CHUNK_SIZE, false);
            else {
                this->row = nullptr; // this == end()
                return *this;
            }
        }

        this->row  = std::make_shared<CSVRow>(std::move(daddy->records.front()));
        this->daddy->records.pop_front();
        return *this;
    }

    CSVReader::iterator CSVReader::iterator::operator++(int) {
        auto temp = *this;
        if (daddy->records.empty()) {
            if (!daddy->eof())
                daddy->read_csv("", ITERATION_CHUNK_SIZE, false);
            else {
                this->row = nullptr; // this == end()
                return temp;
            }
        }

        this->row = std::make_shared<CSVRow>(std::move(daddy->records.front()));
        this->daddy->records.pop_front();
        return temp;
    }

    bool CSVReader::iterator::operator==(const CSVReader::iterator& other) const {
        return other.row == this->row;
    }

    CSVReader::iterator CSVReader::begin() {
        CSVReader::iterator ret(this, std::move(this->records.front()));
        this->records.pop_front();
        return ret;
    }

    CSVReader::iterator CSVReader::end() {
        return CSVReader::iterator(this);
    }
}