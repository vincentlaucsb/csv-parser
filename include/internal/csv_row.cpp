/** @file
 *  Defines the data type used for storing information about a CSV row
 */

#include <cassert>
#include <functional>
#include "csv_row.hpp"
#include "csv_exceptions.hpp"

namespace csv {
    namespace internals {
        csv::string_view get_trimmed(csv::string_view sv, const WhitespaceMap& ws_flags) noexcept
        {
            // Lazy trim only when requested
            size_t start = 0;
            while (start < sv.size() && ws_flags[sv[start] + CHAR_OFFSET]) {
                ++start;
            }

            size_t end = sv.size();
            while (end > start && ws_flags[sv[end - 1] + CHAR_OFFSET]) {
                --end;
            }

            return sv.substr(start, end - start);
        }
    }

#if CSV_EXCEPTIONS_ENABLED
    CSVField CSVRow::operator[](size_t n) const {
        if (n >= this->size()) throw std::runtime_error(internals::CSV_ERROR_INDEX_OUT_OF_BOUNDS);
        return this->make_field(n, this->data);
    }
#endif

#if CSV_EXCEPTIONS_ENABLED
    CSVField CSVRow::operator[](csv::string_view col_name) const {
        auto & col_names = this->data->col_names;
        auto col_pos = col_names->index_of(col_name);
        if (col_pos > -1) {
            return this->operator[](col_pos);
        }

        internals::throw_column_not_found(col_name);
    }
#endif
    CSVField CSVRow::try_field(size_t index, CSVError& error) const {
        error.clear();
        if (index >= size()) {
            internals::fail_static(error, CSVErrorCode::IndexOutOfBounds, internals::CSV_ERROR_INDEX_OUT_OF_BOUNDS);
            return CSVField(csv::string_view());
        }
        return make_field(index, data);
    }

    CSVField CSVRow::try_field(csv::string_view name, CSVError& error) const {
        error.clear();
        const int index = data && data->col_names ? data->col_names->index_of(name) : CSV_NOT_FOUND;
        if (index == CSV_NOT_FOUND) {
            internals::fail(error, CSVErrorCode::ColumnNotFound,
                internals::make_prefixed_message(internals::ERROR_COLUMN_NOT_FOUND, name));
            return CSVField(csv::string_view());
        }
        return try_field(static_cast<size_t>(index), error);
    }

    CSVRow::operator std::vector<std::string>() const {
        std::vector<std::string> ret;
        for (size_t i = 0; i < size(); i++)
            ret.push_back(std::string(this->get_field(i)));

        return ret;
    }

    csv::string_view CSVRow::raw_str() const noexcept {
        if (!data) return csv::string_view();
        const csv::string_view full = data->data;
        if (data_start >= full.size()) return csv::string_view();

        if (data_end != (std::numeric_limits<size_t>::max)()
            && data_end >= data_start
            && data_end <= full.size()) {
            return full.substr(data_start, data_end - data_start);
        }

        const size_t end = full.find('\n', data_start);
        const size_t len = (end == csv::string_view::npos)
            ? (full.size() - data_start)
            : (end - data_start);
        return full.substr(data_start, len);
    }

    std::unordered_map<std::string, std::string> CSVRow::try_to_unordered_map(CSVError& error) const {
        return to_unordered_map_impl(nullptr, error);
    }

    std::unordered_map<std::string, std::string> CSVRow::try_to_unordered_map(
        CSVError& error, const std::vector<std::string>& subset) const {
        return to_unordered_map_impl(&subset, error);
    }

    std::unordered_map<std::string, std::string> CSVRow::to_unordered_map_impl(
        const std::vector<std::string>* subset, CSVError& error) const {
        error.clear();
        std::unordered_map<std::string, std::string> out;
        if (!subset) {
            if (!data || !data->col_names) return out;
            const auto& names = get_col_names();
            if (names.size() < size()) {
                internals::fail_static(error, CSVErrorCode::IndexOutOfBounds, internals::ERROR_COLUMN_INDEX_OUT_OF_BOUNDS);
                return out;
            }
            out.reserve(size());
            for (size_t i = 0; i < size(); ++i) out[names[i]] = std::string(get_field(i));
        } else {
            out.reserve(subset->size());
            for (const auto& name : *subset) {
                auto field = try_field(name, error);
                if (error) return out;
                out[name] = std::string(field.get_sv());
            }
        }
        return out;
    }

#if CSV_EXCEPTIONS_ENABLED
    std::unordered_map<std::string, std::string> CSVRow::to_unordered_map() const {
        CSVError error;
        auto out = try_to_unordered_map(error);
        if (error) throw std::out_of_range(std::string(error.context));
        return out;
    }

    std::unordered_map<std::string, std::string> CSVRow::to_unordered_map(
        const std::vector<std::string>& subset) const {
        CSVError error;
        auto out = try_to_unordered_map(error, subset);
        internals::throw_error(error);
        return out;
    }
#endif

    csv::string_view CSVRow::get_field(size_t index) const
    {
        return this->get_field_impl(index, this->data);
    }

    csv::string_view CSVRow::get_field_safe(size_t index, internals::RawCSVDataPtr _data) const
    {
        return this->get_field_impl(index, _data);
    }

    CSVField CSVRow::make_field(size_t index, const internals::RawCSVDataPtr& _data) const
    {
        const csv::string_view field = this->get_field_impl(index, _data);
        const size_t field_index = this->fields_start + index;
        if (_data->has_field_scalars() && field_index < _data->field_scalars.size()) {
            return CSVField(field, _data->field_scalars[field_index]);
        }

        return CSVField(field);
    }

    bool CSVField::try_parse_decimal(long double& dVal, const char decimalSymbol) {
        // If field has already been parsed to empty, no need to do it aagin:
        if (this->type_ == DataType::CSV_NULL)
                    return false;

        if (this->type_ == DataType::UNKNOWN)
            this->get_value();

        if (this->type_ == DataType::CSV_NULL)
            return false;

        if (this->type_ == DataType::CSV_STRING || this->type_ == DataType::CSV_DOUBLE) {
            double parsed_value = 0;
            if (!classify_scalar::parse_float(this->sv.data(), this->sv.data() + this->sv.size(), parsed_value, decimalSymbol)) {
                if (this->type_ == DataType::CSV_DOUBLE)
                    this->type_ = DataType::CSV_STRING;
                return false;
            }

            this->cache_parsed_value(DataType::CSV_DOUBLE, parsed_value);
        }

        // Integral types are not affected by decimalSymbol and need not be parsed again

        // Either we already had an integral type before, or we we just got any numeric type now.
        if (this->type_ >= DataType::CSV_INT8 && this->type_ <= DataType::CSV_DOUBLE) {
            dVal = this->numeric_value_as_long_double();
            return true;
        }

        // CSV_NULL or CSV_STRING, not numeric
        return false;
    }

    bool CSVField::try_parse_timestamp(std::uint64_t& out) noexcept {
        if (this->type_ == DataType::UNKNOWN)
            this->get_value();

        if (this->type_ == DataType::CSV_TIMESTAMP) {
            out = this->value_.timestamp;
            return true;
        }

        if (this->stores_integral() && this->value_.integer >= 0) {
            out = static_cast<std::uint64_t>(this->value_.integer);
            return true;
        }

        return false;
    }

#ifdef _MSC_VER
#pragma region CSVRow Iterator
#endif
    /** Return an iterator pointing to the first field. */
    CSVRow::iterator CSVRow::begin() const {
        return CSVRow::iterator(this, 0);
    }

    /** Return an iterator pointing to just after the end of the CSVRow.
     *
     *  @warning Attempting to dereference the end iterator results
     *           in dereferencing a null pointer.
     */
    CSVRow::iterator CSVRow::end() const noexcept {
        return CSVRow::iterator(this, (int)this->size());
    }

    CSVRow::reverse_iterator CSVRow::rbegin() const noexcept {
        return std::reverse_iterator<CSVRow::iterator>(this->end());
    }

    CSVRow::reverse_iterator CSVRow::rend() const {
        return std::reverse_iterator<CSVRow::iterator>(this->begin());
    }

    CSV_NON_NULL(2)
    CSVRow::iterator::iterator(const CSVRow* _reader, int _i)
        : daddy(_reader), data(_reader->data), i(_i) {
        if (_i < (int)this->daddy->size())
            this->field = std::make_shared<CSVField>(
                this->daddy->make_field(_i, this->data));
        else
            this->field = nullptr;
    }

    CSVRow::iterator::reference CSVRow::iterator::operator*() const {
        return *(this->field.get());
    }

    CSVRow::iterator::pointer CSVRow::iterator::operator->() const {
        return this->field;
    }

    CSVRow::iterator& CSVRow::iterator::operator++() {
        // Pre-increment operator
        this->i++;
        if (this->i < (int)this->daddy->size())
            this->field = std::make_shared<CSVField>(
                this->daddy->make_field(i, this->data));
        else // Reached the end of row
            this->field = nullptr;
        return *this;
    }

    CSVRow::iterator CSVRow::iterator::operator++(int) {
        // Post-increment operator
        auto temp = *this;
        this->operator++();
        return temp;
    }

    CSVRow::iterator& CSVRow::iterator::operator--() {
        // Pre-decrement operator
        this->i--;
        this->field = std::make_shared<CSVField>(
            this->daddy->make_field(this->i, this->data));
        return *this;
    }

    CSVRow::iterator CSVRow::iterator::operator--(int) {
        // Post-decrement operator
        auto temp = *this;
        this->operator--();
        return temp;
    }
    
    CSVRow::iterator CSVRow::iterator::operator+(difference_type n) const {
        // Allows for iterator arithmetic
        return CSVRow::iterator(this->daddy, i + (int)n);
    }

    CSVRow::iterator CSVRow::iterator::operator-(difference_type n) const {
        // Allows for iterator arithmetic
        return CSVRow::iterator::operator+(-n);
    }

    CSVRow::iterator& CSVRow::iterator::operator+=(difference_type n) {
        *this = *this + n;
        return *this;
    }

    CSVRow::iterator& CSVRow::iterator::operator-=(difference_type n) {
        *this = *this - n;
        return *this;
    }

    CSVRow::iterator::difference_type CSVRow::iterator::operator-(const iterator& other) const noexcept {
        return this->i - other.i;
    }
#ifdef _MSC_VER
#pragma endregion CSVRow Iterator
#endif
}
