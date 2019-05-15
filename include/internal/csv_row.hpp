#pragma once
// Auxiliary data structures for CSV parser

#include "data_type.h"
#include "compatibility.hpp"
#include "giant_string_buffer.hpp"

#include <math.h>
#include <vector>
#include <string>
#include <iterator>
#include <unordered_map> // For ColNames
#include <memory> // For CSVField
#include <limits> // For CSVField

namespace csv {
    /**
    * @class CSVField
    * @brief Data type representing individual CSV values. 
    *        CSVFields can be obtained by using CSVRow::operator[]
    */
    class CSVField {
    public:
        CSVField(csv::string_view _sv) : sv(_sv) { };

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
        */
        template<typename T=csv::string_view> T get() {
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

        DataType type();
        bool is_null() { return type() == CSV_NULL; }
        bool is_str() { return type() == CSV_STRING; }
        bool is_num() { return type() >= CSV_INT; }
        bool is_int() {
            return (type() >= CSV_INT) && (type() <= CSV_LONG_LONG_INT);
        }
        bool is_float() { return type() == CSV_DOUBLE; };

    private:
        long double value = 0;
        csv::string_view sv = "";
        int _type = -1;
        void get_value();
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
        CSVRow(internals::BufferPtr _str) : str(_str)
        {
            this->row_str = _str->get_row();
            this->splits = _str->get_splits();
        };

        /**
        CSVRow(
            std::string _row_str,
            std::vector<unsigned short>&& _splits,
            std::shared_ptr<internals::ColNames> _cnames = nullptr
            ) :
            str(std::make_shared<std::string>(_row_str)),
            splits(std::move(_splits)),
            col_names(_cnames)
        {
            row_str = csv::string_view(this->str->buffer->c_str());
        };*/

        bool empty() const { return this->row_str.empty(); }
        size_t size() const;

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
		internals::BufferPtr str = nullptr;
		csv::string_view row_str = "";
        internals::ColumnPositions splits;
    };

    // get() specializations
    template<>
    inline std::string CSVField::get<std::string>() {
        return std::string(this->sv);
    }

    template<>
    inline csv::string_view CSVField::get<csv::string_view>() {
        return this->sv;
    }

    template<>
    inline long double CSVField::get<long double>() {
        if (!is_num())
            throw std::runtime_error("Not a number.");

        return this->value;
    }
}