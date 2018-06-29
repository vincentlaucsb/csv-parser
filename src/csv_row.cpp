#include "csv_parser.hpp"

namespace csv {
    bool CSVRow::CSVField::operator==(const std::string& other) const {
        return other == this->sv;
    }

    size_t CSVRow::size() const {
        return splits.size() + 1;
    }

    std::string_view CSVRow::get_string_view(size_t n) const {
        std::string_view ret(this->row_str);
        size_t beg = 0, end = row_str.size();

        if (!splits.empty()) {
            if (splits.size() == 1 || n == 0) {
                if (n == 0) end = this->splits[0];
                else beg = this->splits[0];
            }
            else {
                beg = this->splits.at(n - 1);
                if (n != splits.size()) end = this->splits.at(n);
            }
        }
        
        return ret.substr(
            beg,
            end - beg // Number of characters
        );
    }

    CSVRow::CSVField CSVRow::operator[](size_t n) const {
        return CSVField(this->get_string_view(n));
    }

    CSVRow::operator std::vector<std::string>() const {
        /** Convert this CSVRow into a vector of strings.
         *  **Note**: This is an inefficient fuction and should be should sparingly.
         */
        std::vector<std::string> ret;
        for (size_t i = 0; i < size(); i++)
            ret.push_back(std::string(this->get_string_view(i)));
        return ret;
    }
}