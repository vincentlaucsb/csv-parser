/** @file
 *  Defines the data type used for storing information about a CSV row
 */

#include <cassert>
#include <functional>
#include "csv_row.hpp"

namespace csv {

    /** Return a CSVField object corrsponding to the nth value in the row.
     *
     *  @note This method performs bounds checking, and will throw an
     *        `std::runtime_error` if n is invalid.
     *
     *  @complexity
     *  Constant, by calling csv::CSVRow::get_csv::string_view()
     *
     */
    CSV_INLINE CSVField CSVRow::operator[](size_t n) const {
        return CSVField(this->get_field(n));
    }

    /** Retrieve a value by its associated column name. If the column
     *  specified can't be round, a runtime error is thrown.
     *
     *  @complexity
     *  Constant. This calls the other CSVRow::operator[]() after
     *  converting column names into indices using a hash table.
     *
     *  @param[in] col_name The column to look for
     */
    CSV_INLINE CSVField CSVRow::operator[](const std::string& col_name) const {
        auto & col_names = this->data->col_names;
        auto col_pos = col_names->index_of(col_name);
        if (col_pos > -1) {
            return this->operator[](col_pos);
        }

        throw std::runtime_error("Can't find a column named " + col_name);
    }

    CSV_INLINE CSVRow::operator std::vector<std::string>() const {
        std::vector<std::string> ret;
        for (size_t i = 0; i < size(); i++)
            ret.push_back(std::string(this->get_field(i)));

        return ret;
    }

    /** Build a map from column names to values for a given row. */
    CSV_INLINE std::unordered_map<std::string, std::string> CSVRow::to_unordered_map() const {
        std::unordered_map<std::string, std::string> row_map;
        row_map.reserve(this->size());

        for (size_t i = 0; i < this->size(); i++) {
            auto col_name = (*this->data->col_names)[i];
            row_map[col_name] = this->operator[](i).get<std::string>();
        }

        return row_map;
    }

    /**
     * Build a map from column names to values for a given row.
     * 
     * @param[in] subset Vector of column names to include in the map.
     */
    CSV_INLINE std::unordered_map<std::string, std::string> CSVRow::to_unordered_map(
        const std::vector<std::string>& subset
    ) const {
        std::unordered_map<std::string, std::string> row_map;
        row_map.reserve(subset.size());

        for (const auto& col_name : subset)
            row_map[col_name] = this->operator[](col_name).get<std::string>();

        return row_map;
    }

    CSV_INLINE csv::string_view CSVRow::get_field(size_t index) const
    {
        return this->get_field_impl(index, this->data);
    }

    CSV_INLINE csv::string_view CSVRow::get_field_safe(size_t index, internals::RawCSVDataPtr _data) const
    {
        return this->get_field_impl(index, _data);
    }

    CSV_INLINE bool CSVField::try_parse_decimal(long double& dVal, const char decimalSymbol) {
        // If field has already been parsed to empty, no need to do it aagin:
        if (this->_type == DataType::CSV_NULL)
                    return false;

        // Not yet parsed or possibly parsed with other decimalSymbol
        if (this->_type == DataType::UNKNOWN || this->_type == DataType::CSV_STRING || this->_type == DataType::CSV_DOUBLE)
            this->_type = internals::data_type(this->sv, &this->value, decimalSymbol); // parse again

        // Integral types are not affected by decimalSymbol and need not be parsed again

        // Either we already had an integral type before, or we we just got any numeric type now.
        if (this->_type >= DataType::CSV_INT8 && this->_type <= DataType::CSV_DOUBLE) {
            dVal = this->value;
            return true;
        }

        // CSV_NULL or CSV_STRING, not numeric
        return false;
    }

#ifdef _MSC_VER
#pragma region CSVRow Iterator
#endif
    /** Return an iterator pointing to the first field. */
    CSV_INLINE CSVRow::iterator CSVRow::begin() const {
        return CSVRow::iterator(this, 0);
    }

    /** Return an iterator pointing to just after the end of the CSVRow.
     *
     *  @warning Attempting to dereference the end iterator results
     *           in dereferencing a null pointer.
     */
    CSV_INLINE CSVRow::iterator CSVRow::end() const noexcept {
        return CSVRow::iterator(this, (int)this->size());
    }

    CSV_INLINE CSVRow::reverse_iterator CSVRow::rbegin() const noexcept {
        return std::reverse_iterator<CSVRow::iterator>(this->end());
    }

    CSV_INLINE CSVRow::reverse_iterator CSVRow::rend() const {
        return std::reverse_iterator<CSVRow::iterator>(this->begin());
    }

    CSV_INLINE CSV_NON_NULL(2)
    CSVRow::iterator::iterator(const CSVRow* _reader, int _i)
        : daddy(_reader), data(_reader->data), i(_i) {
        if (_i < (int)this->daddy->size())
            this->field = std::make_shared<CSVField>(
                CSVField(this->daddy->get_field_safe(_i, this->data)));
        else
            this->field = nullptr;
    }

    CSV_INLINE CSVRow::iterator::reference CSVRow::iterator::operator*() const {
        return *(this->field.get());
    }

    CSV_INLINE CSVRow::iterator::pointer CSVRow::iterator::operator->() const {
        return this->field;
    }

    CSV_INLINE CSVRow::iterator& CSVRow::iterator::operator++() {
        // Pre-increment operator
        this->i++;
        if (this->i < (int)this->daddy->size())
            this->field = std::make_shared<CSVField>(
                CSVField(this->daddy->get_field_safe(i, this->data)));
        else // Reached the end of row
            this->field = nullptr;
        return *this;
    }

    CSV_INLINE CSVRow::iterator CSVRow::iterator::operator++(int) {
        // Post-increment operator
        auto temp = *this;
        this->operator++();
        return temp;
    }

    CSV_INLINE CSVRow::iterator& CSVRow::iterator::operator--() {
        // Pre-decrement operator
        this->i--;
        this->field = std::make_shared<CSVField>(
            CSVField(this->daddy->get_field_safe(this->i, this->data)));
        return *this;
    }

    CSV_INLINE CSVRow::iterator CSVRow::iterator::operator--(int) {
        // Post-decrement operator
        auto temp = *this;
        this->operator--();
        return temp;
    }
    
    CSV_INLINE CSVRow::iterator CSVRow::iterator::operator+(difference_type n) const {
        // Allows for iterator arithmetic
        return CSVRow::iterator(this->daddy, i + (int)n);
    }

    CSV_INLINE CSVRow::iterator CSVRow::iterator::operator-(difference_type n) const {
        // Allows for iterator arithmetic
        return CSVRow::iterator::operator+(-n);
    }
#ifdef _MSC_VER
#pragma endregion CSVRow Iterator
#endif
}
