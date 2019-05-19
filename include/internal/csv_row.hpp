/** @file
 *  Defines the data type used for storing information about a CSV row
 */

#pragma once
#include <math.h>
#include <vector>
#include <string>
#include <iterator>
#include <unordered_map> // For ColNames
#include <memory> // For CSVField
#include <limits> // For CSVField

#include "data_type.h"
#include "compatibility.hpp"
#include "row_buffer.hpp"

namespace csv {
    /**
    * @class CSVField
    * @brief Data type representing individual CSV values. 
    *        CSVFields can be obtained by using CSVRow::operator[]
    */
    class CSVField {
    public:
        /** Constructs a CSVField from a string_view */
        constexpr CSVField(csv::string_view _sv) : sv(_sv) { };

        /** Returns the value casted to the requested type, performing type checking before.
        *  An std::runtime_error will be thrown if a type mismatch occurs, with the exception
        *  of T = std::string, in which the original string representation is always returned.
        *  Converting long ints to ints will be checked for overflow.
        *
        *  **Valid options for T**:
        *   - std::string or csv::string_view
        *   - int
        *   - long
        *   - long long
        *   - double
        *   - long double
        *
        @warning Any string_views returned are only guaranteed to be valid
        *        if the parent CSVRow is still alive. If you are concerned
        *        about object lifetimes, then grab a std::string or a
        *        numeric value.
        *
        */
        template<typename T=std::string> T get() {
            auto dest_type = internals::type_num<T>();
            if (dest_type >= CSV_INT && is_num()) {
                if (internals::type_num<T>() < this->type())
                    throw std::runtime_error("Overflow error.");

                return static_cast<T>(this->value);
            }

            throw std::runtime_error("Attempted to convert a value of type " + 
                internals::type_name(type()) + " to " + internals::type_name(dest_type) + ".");
        }

        bool operator==(csv::string_view other) const;
        bool operator==(const long double& other);

        /** Returns true if field is an empty string or string of whitespace characters */
        CONSTEXPR bool is_null() { return type() == CSV_NULL; }

        /** Returns true if field is a non-numeric string */
        CONSTEXPR bool is_str() { return type() == CSV_STRING; }

        /** Returns true if field is an integer or float */
        CONSTEXPR bool is_num() { return type() >= CSV_INT; }

        /** Returns true if field is an integer */
        CONSTEXPR bool is_int() {
            return (type() >= CSV_INT) && (type() <= CSV_LONG_LONG_INT);
        }

        /** Returns true if field is a float*/
        CONSTEXPR bool is_float() { return type() == CSV_DOUBLE; };

        /** Return the type of the underlying CSV data */
        CONSTEXPR DataType type() {
            this->get_value();
            return (DataType)_type;
        }

    private:
        long double value = 0;    /**< Cached numeric value */
        csv::string_view sv = ""; /**< A pointer to this field's text */
        DataType _type = UNKNOWN; /**< Cached data type value */
        CONSTEXPR void get_value() {
            /* Check to see if value has been cached previously, if not
             * evaluate it
             */
            if (_type < 0) {
                this->_type = internals::data_type(this->sv, &this->value);
            }
        }
    };

    /**
     * @class CSVRow 
     * @brief Data structure for representing CSV rows
     *
     * Internally, a CSVRow consists of:
     *  - A pointer to the original column names
     *  - A string containing the entire CSV row (row_str)
     *  - An array of positions in that string where individual fields begin (splits)
     *
     * CSVRow::operator[] uses splits to compute a string_view over row_str.
     *
     */
    class CSVRow {
    public:
        CSVRow() = default;

        /** Construct a CSVRow from a RawRowBuffer. Should be called by CSVReader::write_record. */
        CSVRow(const internals::BufferPtr& _str) : buffer(_str)
        {
            this->row_str = _str->get_row();

            auto splits = _str->get_splits();
            this->start = splits.start;
            this->n_cols = splits.n_cols;
        };

        /** Constructor for testing */
        CSVRow(const std::string& str, const std::vector<unsigned short>& splits, 
            const std::shared_ptr<internals::ColNames>& col_names)
            : CSVRow(internals::BufferPtr(new internals::RawRowBuffer(str, splits, col_names))) {};

        /** Indicates whether row is empty or not */
        CONSTEXPR bool empty() const { return this->row_str.empty(); }

        /** @brief Return the number of fields in this row */
        CONSTEXPR size_t size() const { return this->n_cols; }

        /** @name Value Retrieval */
        ///@{
        CSVField operator[](size_t n) const;
        CSVField operator[](const std::string&) const;
        csv::string_view get_string_view(size_t n) const;
        operator std::vector<std::string>() const;
        ///@}

        /** @brief A random access iterator over the contents of a CSV row.
         *         Each iterator points to a CSVField.
         */
        class iterator {
        public:
            #ifndef DOXYGEN_SHOULD_SKIP_THIS
            using value_type = CSVField;
            using difference_type = int;

            // Using CSVField * as pointer type causes segfaults in MSVC debug builds
            // but using shared_ptr as pointer type won't compile in g++
            #ifdef _MSC_BUILD
            using pointer = std::shared_ptr<CSVField> ;
            #else
            using pointer = CSVField * ;
            #endif

            using reference = CSVField & ;
            using iterator_category = std::random_access_iterator_tag;
            #endif

            iterator(const CSVRow*, int i);

            reference operator*() const;
            pointer operator->() const;

            iterator operator++(int);
            iterator& operator++();
            iterator operator--(int);
            iterator& operator--();
            iterator operator+(difference_type n) const;
            iterator operator-(difference_type n) const;

            bool operator==(const iterator&) const;
            bool operator!=(const iterator& other) const { return !operator==(other); }

            #ifndef NDEBUG
            friend CSVRow;
            #endif

        private:
            const CSVRow * daddy = nullptr;            // Pointer to parent
            std::shared_ptr<CSVField> field = nullptr; // Current field pointed at
            int i = 0;                                 // Index of current field
        };

        /** @brief A reverse iterator over the contents of a CSVRow. */
        using reverse_iterator = std::reverse_iterator<iterator>;

        /** @name Iterators
         *  @brief Each iterator points to a CSVField object.
         */
        ///@{
        iterator begin() const;
        iterator end() const;
        reverse_iterator rbegin() const;
        reverse_iterator rend() const;
        ///@}

    private:
        /** Get the index in CSVRow's text buffer where the n-th field begins */
        unsigned short split_at(size_t n) const;

		internals::BufferPtr buffer = nullptr; /**< Memory buffer containing data for this row. */
		csv::string_view row_str = "";         /**< Text data for this row */
        size_t start;                          /**< Where in split buffer this row begins */
        unsigned short n_cols;                 /**< Numbers of columns this row has */
    };

#pragma region CSVField::get Specializations
    /** Retrieve this field's original string */
    template<>
    inline std::string CSVField::get<std::string>() {
        return std::string(this->sv);
    }

    /** Retrieve a view over this field's string
     *
     *  @warning This string_view is only guaranteed to be valid as long as this 
     *           CSVRow is still alive.
     */
    template<>
    CONSTEXPR csv::string_view CSVField::get<csv::string_view>() {
        return this->sv;
    }

    /** Retrieve this field's value as a long double */
    template<>
    CONSTEXPR long double CSVField::get<long double>() {
        if (!is_num())
            throw std::runtime_error("Not a number.");

        return this->value;
    }
#pragma endregion CSVField::get Specializations
}