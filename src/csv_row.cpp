#include "csv_row.hpp"

namespace csv {
    /*
     * Column Names
     */

    ColNames::ColNames(
        const std::vector<std::string>& _cnames
    ) : col_names(_cnames) {
        for (size_t i = 0; i < _cnames.size(); i++) {
            this->col_pos[_cnames[i]] = i;
        }
    }

    std::vector<std::string> ColNames::get_col_names() const {
        return this->col_names;
    }

    size_t ColNames::size() const {
        return this->col_names.size();
    }

    bool CSVField::operator==(const std::string& other) const {
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

    CSVField CSVRow::operator[](size_t n) const {
        return CSVField(this->get_string_view(n));
    }

    CSVField CSVRow::operator[](const std::string& col_name) const {
        auto col_pos = this->col_names->col_pos.find(col_name);
        if (col_pos != this->col_names->col_pos.end())
            return this->operator[](col_pos->second);

        throw std::runtime_error("Can't find a column named " + col_name);
    }

    CSVRow::operator std::vector<std::string>() const {
        /** Convert this CSVRow into a vector of strings.
         *  **Note**: This is a less efficient method of accessing data than using the [] operator.
         */

        std::vector<std::string> ret;
        for (size_t i = 0; i < size(); i++)
            ret.push_back(std::string(this->get_string_view(i)));

        return ret;
    }

    /*
     * CSVField Methods
     */
    void CSVField::get_value() {
        // Check to see if value has been cached previously, if not
        // evaluate it
        if (_type < 0) {
            auto dtype = helpers::data_type(this->sv, &this->value);
            this->_type = (int)dtype;
        }
    }
}