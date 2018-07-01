#include "csv_row.hpp"

namespace csv {
    namespace internals {
        //
        // ColNames
        //

        ColNames::ColNames(const std::vector<std::string>& _cnames)
            : col_names(_cnames) {
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
    }

    bool CSVField::operator==(const std::string& other) const {
        return other == this->sv;
    }

    size_t CSVRow::size() const {
        /** Return the number of fields in this row */
        return splits.size() + 1;
    }

    /** @brief      Return a string view of the nth field
     *  @complexity Constant
     */
    std::string_view CSVRow::get_string_view(size_t n) const {
        std::string_view ret(this->row_str);
        size_t beg = 0, end = row_str.size(),
            r_size = this->size();

        if (n > r_size)
            throw std::runtime_error("Index out of bounds.");

        if (!splits.empty()) {
            if (n == 0 || r_size == 2) {
                if (n == 0) end = this->splits[0];
                else beg = this->splits[0];
            }
            else {
                beg = this->splits[n - 1];
                if (n != r_size - 1) end = this->splits[n];
            }
        }
        
        return ret.substr(
            beg,
            end - beg // Number of characters
        );
    }

    /** @brief Return a CSVField object corrsponding to the nth value in the row.
     *
     *  This method performs boounds checking, and will throw an std::runtime_error
     *  if n is invalid.
     *
     *  @complexity Constant, by calling CSVRow::get_string_view()
     *
     */
    CSVField CSVRow::operator[](size_t n) const {
        return CSVField(this->get_string_view(n));
    }

    /** @brief Retrieve a value by its associated column name. If the column
     *         specified can't be round, a runtime error is thrown.
     *
     *  @complexity Constant. This calls the other CSVRow::operator[]() after
                    converting column names into indices using a hash table.
     *
     *  @param[in] col_name The column to look for
     */
    CSVField CSVRow::operator[](const std::string& col_name) const {
        auto col_pos = this->col_names->col_pos.find(col_name);
        if (col_pos != this->col_names->col_pos.end())
            return this->operator[](col_pos->second);

        throw std::runtime_error("Can't find a column named " + col_name);
    }

    CSVRow::operator std::vector<std::string>() const {
        /** Convert this CSVRow into a vector of strings.
         *  **Note**: This is a less efficient method of
         *  accessing data than using the [] operator.
         */

        std::vector<std::string> ret;
        for (size_t i = 0; i < size(); i++)
            ret.push_back(std::string(this->get_string_view(i)));

        return ret;
    }

    //////////////////////
    // CSVField Methods //
    //////////////////////

    /**< @brief Return the type number of the stored value in
     *          accordance with the DataType enum
     */
    DataType CSVField::type() {
        this->get_value();
        return (DataType)_type;
    }

    #ifndef DOXYGEN_SHOULD_SKIP_THIS
    void CSVField::get_value() {
        /* Check to see if value has been cached previously, if not
         * evaluate it
         */
        if (_type < 0) {
            auto dtype = internals::data_type(this->sv, &this->value);
            this->_type = (int)dtype;
        }
    }
    #endif
}