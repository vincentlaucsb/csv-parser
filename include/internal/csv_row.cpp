/** @file
 *  Defines the data type used for storing information about a CSV row
 */

#include <cassert>
#include <functional>
#include "csv_row.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE RawCSVField& CSVFieldList::operator[](size_t n) const {
            const size_t page_no = n / _single_buffer_capacity;
            const size_t buffer_idx = (page_no < 1) ? n : n % _single_buffer_capacity;
            return this->buffers[page_no][buffer_idx];
        }

        CSV_INLINE void CSVFieldList::allocate() {
            buffers.push_back(std::unique_ptr<RawCSVField[]>(new RawCSVField[_single_buffer_capacity]));

            _current_buffer_size = 0;
            _back = buffers.back().get();
        }
    }

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

    CSV_INLINE csv::string_view CSVRow::get_field(size_t index) const
    {
        using internals::ParseFlags;

        if (index >= this->size())
            throw std::runtime_error("Index out of bounds.");

        const size_t field_index = this->fields_start + index;
        auto& field = this->data->fields[field_index];
        auto field_str = csv::string_view(this->data->data).substr(this->data_start + field.start);

        if (field.has_double_quote) {
            auto& value = this->data->double_quote_fields[field_index];
            if (value.empty()) {
                bool prev_ch_quote = false;
                for (size_t i = 0; i < field.length; i++) {
                    if (this->data->parse_flags[field_str[i] + 128] == ParseFlags::QUOTE) {
                        if (prev_ch_quote) {
                            prev_ch_quote = false;
                            continue;
                        }
                        else {
                            prev_ch_quote = true;
                        }
                    }

                    value += field_str[i];
                }
            }

            return csv::string_view(value);
        }

        return field_str.substr(0, field.length);
    }

    CSV_INLINE bool CSVField::try_parse_hex(int& parsedValue) {
        size_t start = 0, end = 0;

        // Trim out whitespace chars
        for (; start < this->sv.size() && this->sv[start] == ' '; start++);
        for (end = start; end < this->sv.size() && this->sv[end] != ' '; end++);
        
        int value_ = 0;

        size_t digits = (end - start);
        size_t base16_exponent = digits - 1;

        if (digits == 0) return false;

        for (const auto& ch : this->sv.substr(start, digits)) {
            int digit = 0;

            switch (ch) {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                digit = static_cast<int>(ch - '0');
                break;
            case 'a':
            case 'A':
                digit = 10;
                break;
            case 'b':
            case 'B':
                digit = 11;
                break;
            case 'c':
            case 'C':
                digit = 12;
                break;
            case 'd':
            case 'D':
                digit = 13;
                break;
            case 'e':
            case 'E':
                digit = 14;
                break;
            case 'f':
            case 'F':
                digit = 15;
                break;
            default:
                return false;
            }

            value_ += digit * (int)pow(16, (double)base16_exponent);
            base16_exponent--;
        }

        parsedValue = value_;
        return true;
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

    CSV_INLINE HEDLEY_NON_NULL(2)
    CSVRow::iterator::iterator(const CSVRow* _reader, int _i)
        : daddy(_reader), i(_i) {
        if (_i < (int)this->daddy->size())
            this->field = std::make_shared<CSVField>(
                this->daddy->operator[](_i));
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
                this->daddy->operator[](i));
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
            this->daddy->operator[](this->i));
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
