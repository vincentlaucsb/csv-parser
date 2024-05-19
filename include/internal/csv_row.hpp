/** @file
 *  Defines the data type used for storing information about a CSV row
 */

#pragma once
#include <cmath>
#include <iterator>
#include <memory> // For CSVField
#include <limits> // For CSVField
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <sstream>
#include <vector>

#include "common.hpp"
#include "data_type.h"
#include "col_names.hpp"

namespace csv {
    namespace internals {
        class IBasicCSVParser;

        static const std::string ERROR_NAN = "Not a number.";
        static const std::string ERROR_OVERFLOW = "Overflow error.";
        static const std::string ERROR_FLOAT_TO_INT =
            "Attempted to convert a floating point value to an integral type.";
        static const std::string ERROR_NEG_TO_UNSIGNED = "Negative numbers cannot be converted to unsigned types.";
    
        std::string json_escape_string(csv::string_view s) noexcept;

        /** A barebones class used for describing CSV fields */
        struct RawCSVField {
            RawCSVField() = default;
            RawCSVField(size_t _start, size_t _length, bool _double_quote = false) {
                start = _start;
                length = _length;
                has_double_quote = _double_quote;
            }

            /** The start of the field, relative to the beginning of the row */
            size_t start;

            /** The length of the row, ignoring quote escape characters */
            size_t length; 

            /** Whether or not the field contains an escaped quote */
            bool has_double_quote;
        };

        /** A class used for efficiently storing RawCSVField objects and expanding as necessary
         *
         *  @par Implementation
         *  This data structure stores RawCSVField in continguous blocks. When more capacity
         *  is needed, a new block is allocated, but previous data stays put.
         *
         *  @par Thread Safety
         *  This class may be safely read from multiple threads and written to from one,
         *  as long as the writing thread does not actively touch fields which are being
         *  read.
         */
        class CSVFieldList {
        public:
            /** Construct a CSVFieldList which allocates blocks of a certain size */
            CSVFieldList(size_t single_buffer_capacity = (size_t)(internals::PAGE_SIZE / sizeof(RawCSVField))) :
                _single_buffer_capacity(single_buffer_capacity) {
                this->allocate();
            }

            // No copy constructor
            CSVFieldList(const CSVFieldList& other) = delete;

            // CSVFieldArrays may be moved
            CSVFieldList(CSVFieldList&& other) :
                _single_buffer_capacity(other._single_buffer_capacity) {
                buffers = std::move(other.buffers);
                _current_buffer_size = other._current_buffer_size;
                _back = other._back;
            }

            ~CSVFieldList() {
                for (auto& buffer : buffers)
                    delete[] buffer;
            }

            template <class... Args>
            void emplace_back(Args&&... args) {
                if (this->_current_buffer_size == this->_single_buffer_capacity) {
                    this->allocate();
                }

                *(_back++) = RawCSVField(std::forward<Args>(args)...);
                _current_buffer_size++;
            }

            size_t size() const noexcept {
                return this->_current_buffer_size + ((this->buffers.size() - 1) * this->_single_buffer_capacity);
            }

            RawCSVField& operator[](size_t n) const;

        private:
            const size_t _single_buffer_capacity;

            std::vector<RawCSVField*> buffers = {};

            /** Number of items in the current buffer */
            size_t _current_buffer_size = 0;

            /** Pointer to the current empty field */
            RawCSVField* _back = nullptr;

            /** Allocate a new page of memory */
            void allocate();
        };


        /** A class for storing raw CSV data and associated metadata */
        struct RawCSVData {
            std::shared_ptr<void> _data = nullptr;
            csv::string_view data = "";

            internals::CSVFieldList fields;

            std::unordered_set<size_t> has_double_quotes = {};

            // TODO: Consider replacing with a more thread-safe structure
            std::unordered_map<size_t, std::string> double_quote_fields = {};

            internals::ColNamesPtr col_names = nullptr;
            internals::ParseFlagMap parse_flags;
            internals::WhitespaceMap ws_flags;
        };

        using RawCSVDataPtr = std::shared_ptr<RawCSVData>;
    }

    /**
    * @class CSVField
    * @brief Data type representing individual CSV values.
    *        CSVFields can be obtained by using CSVRow::operator[]
    */
    class CSVField {
    public:
        /** Constructs a CSVField from a string_view */
        constexpr explicit CSVField(csv::string_view _sv) noexcept : sv(_sv) { };

        operator std::string() const {
            return std::string("<CSVField> ") + std::string(this->sv);
        }

        /** Returns the value casted to the requested type, performing type checking before.
        *
        *  \par Valid options for T
        *   - std::string or csv::string_view
        *   - signed integral types (signed char, short, int, long int, long long int)
        *   - floating point types (float, double, long double)
        *   - unsigned integers are not supported at this time, but may be in a later release
        *
        *  \par Invalid conversions
        *   - Converting non-numeric values to any numeric type
        *   - Converting floating point values to integers
        *   - Converting a large integer to a smaller type that will not hold it
        *
        *  @note    This method is capable of parsing scientific E-notation.
        *           See [this page](md_docs_source_scientific_notation.html)
        *           for more details.
        *
        *  @throws  std::runtime_error Thrown if an invalid conversion is performed.
        *
        *  @warning Currently, conversions to floating point types are not
        *           checked for loss of precision
        *
        *  @warning Any string_views returned are only guaranteed to be valid
        *           if the parent CSVRow is still alive. If you are concerned
        *           about object lifetimes, then grab a std::string or a
        *           numeric value.
        *
        */
        template<typename T = std::string> T get() {
            IF_CONSTEXPR(std::is_arithmetic<T>::value) {
                // Note: this->type() also converts the CSV value to float
                if (this->type() <= DataType::CSV_STRING) {
                    throw std::runtime_error(internals::ERROR_NAN);
                }
            }

            IF_CONSTEXPR(std::is_integral<T>::value) {
                // Note: this->is_float() also converts the CSV value to float
                if (this->is_float()) {
                    throw std::runtime_error(internals::ERROR_FLOAT_TO_INT);
                }

                IF_CONSTEXPR(std::is_unsigned<T>::value) {
                    if (this->value < 0) {
                        throw std::runtime_error(internals::ERROR_NEG_TO_UNSIGNED);
                    }
                }
            }

            // Allow fallthrough from previous if branch
            IF_CONSTEXPR(!std::is_floating_point<T>::value) {
                IF_CONSTEXPR(std::is_unsigned<T>::value) {
                    // Quick hack to perform correct unsigned integer boundary checks
                    if (this->value > internals::get_uint_max<sizeof(T)>()) {
                        throw std::runtime_error(internals::ERROR_OVERFLOW);
                    }
                }
                else if (internals::type_num<T>() < this->_type) {
                    throw std::runtime_error(internals::ERROR_OVERFLOW);
                }
            }

            return static_cast<T>(this->value);
        }

        /** Parse a hexadecimal value, returning false if the value is not hex. */
        bool try_parse_hex(int& parsedValue);

        /** Compares the contents of this field to a numeric value. If this
         *  field does not contain a numeric value, then all comparisons return
         *  false.
         *
         *  @note    Floating point values are considered equal if they are within
         *           `0.000001` of each other.
         *
         *  @warning Multiple numeric comparisons involving the same field can
         *           be done more efficiently by calling the CSVField::get<>() method.
         *
         *  @sa      csv::CSVField::operator==(const char * other)
         *  @sa      csv::CSVField::operator==(csv::string_view other)
         */
        template<typename T>
        CONSTEXPR_14 bool operator==(T other) const noexcept
        {
            static_assert(std::is_arithmetic<T>::value,
                "T should be a numeric value.");

            if (this->_type != DataType::UNKNOWN) {
                if (this->_type == DataType::CSV_STRING) {
                    return false;
                }

                return internals::is_equal(value, static_cast<long double>(other), 0.000001L);
            }

            long double out = 0;
            if (internals::data_type(this->sv, &out) == DataType::CSV_STRING) {
                return false;
            }

            return internals::is_equal(out, static_cast<long double>(other), 0.000001L);
        }

        /** Return a string view over the field's contents */
        CONSTEXPR csv::string_view get_sv() const noexcept { return this->sv; }

        /** Returns true if field is an empty string or string of whitespace characters */
        CONSTEXPR_14 bool is_null() noexcept { return type() == DataType::CSV_NULL; }

        /** Returns true if field is a non-numeric, non-empty string */
        CONSTEXPR_14 bool is_str() noexcept { return type() == DataType::CSV_STRING; }

        /** Returns true if field is an integer or float */
        CONSTEXPR_14 bool is_num() noexcept { return type() >= DataType::CSV_INT8; }

        /** Returns true if field is an integer */
        CONSTEXPR_14 bool is_int() noexcept {
            return (type() >= DataType::CSV_INT8) && (type() <= DataType::CSV_INT64);
        }

        /** Returns true if field is a floating point value */
        CONSTEXPR_14 bool is_float() noexcept { return type() == DataType::CSV_DOUBLE; };

        /** Return the type of the underlying CSV data */
        CONSTEXPR_14 DataType type() noexcept {
            this->get_value();
            return _type;
        }

    private:
        long double value = 0;    /**< Cached numeric value */
        csv::string_view sv = ""; /**< A pointer to this field's text */
        DataType _type = DataType::UNKNOWN; /**< Cached data type value */
        CONSTEXPR_14 void get_value() noexcept {
            /* Check to see if value has been cached previously, if not
             * evaluate it
             */
            if ((int)_type < 0) {
                this->_type = internals::data_type(this->sv, &this->value);
            }
        }
    };

    /** Data structure for representing CSV rows */
    class CSVRow {
    public:
        friend internals::IBasicCSVParser;

        CSVRow() = default;
        
        /** Construct a CSVRow from a RawCSVDataPtr */
        CSVRow(internals::RawCSVDataPtr _data) : data(_data) {}
        CSVRow(internals::RawCSVDataPtr _data, size_t _data_start, size_t _field_bounds)
            : data(_data), data_start(_data_start), fields_start(_field_bounds) {}

        /** Indicates whether row is empty or not */
        CONSTEXPR bool empty() const noexcept { return this->size() == 0; }

        /** Return the number of fields in this row */
        CONSTEXPR size_t size() const noexcept { return row_length; }

        /** @name Value Retrieval */
        ///@{
        CSVField operator[](size_t n) const;
        CSVField operator[](const std::string&) const;
        std::string to_json(const std::vector<std::string>& subset = {}) const;
        std::string to_json_array(const std::vector<std::string>& subset = {}) const;

        /** Retrieve this row's associated column names */
        std::vector<std::string> get_col_names() const {
            return this->data->col_names->get_col_names();
        }

        /** Convert this CSVRow into a vector of strings.
         *  **Note**: This is a less efficient method of
         *  accessing data than using the [] operator.
         */
        operator std::vector<std::string>() const;
        ///@}

        /** A random access iterator over the contents of a CSV row.
         *  Each iterator points to a CSVField.
         */
        class iterator {
        public:
#ifndef DOXYGEN_SHOULD_SKIP_THIS
            using value_type = CSVField;
            using difference_type = int;

            // Using CSVField * as pointer type causes segfaults in MSVC debug builds
            // but using shared_ptr as pointer type won't compile in g++
#ifdef _MSC_BUILD
            using pointer = std::shared_ptr<CSVField>;
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

            /** Two iterators are equal if they point to the same field */
            CONSTEXPR bool operator==(const iterator& other) const noexcept {
                return this->i == other.i;
            };

            CONSTEXPR bool operator!=(const iterator& other) const noexcept { return !operator==(other); }

#ifndef NDEBUG
            friend CSVRow;
#endif

        private:
            const CSVRow * daddy = nullptr;            // Pointer to parent
            std::shared_ptr<CSVField> field = nullptr; // Current field pointed at
            int i = 0;                                 // Index of current field
        };

        /** A reverse iterator over the contents of a CSVRow. */
        using reverse_iterator = std::reverse_iterator<iterator>;

        /** @name Iterators
         *  @brief Each iterator points to a CSVField object.
         */
         ///@{
        iterator begin() const;
        iterator end() const noexcept;
        reverse_iterator rbegin() const noexcept;
        reverse_iterator rend() const;
        ///@}

    private:
        /** Retrieve a string view corresponding to the specified index */
        csv::string_view get_field(size_t index) const;

        internals::RawCSVDataPtr data;

        /** Where in RawCSVData.data we start */
        size_t data_start = 0;

        /** Where in the RawCSVDataPtr.fields array we start */
        size_t fields_start = 0;

        /** How many columns this row spans */
        size_t row_length = 0;
    };

#ifdef _MSC_VER
#pragma region CSVField::get Specializations
#endif
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
    CONSTEXPR_14 csv::string_view CSVField::get<csv::string_view>() {
        return this->sv;
    }

    /** Retrieve this field's value as a long double */
    template<>
    CONSTEXPR_14 long double CSVField::get<long double>() {
        if (!is_num())
            throw std::runtime_error(internals::ERROR_NAN);

        return this->value;
    }
#ifdef _MSC_VER
#pragma endregion CSVField::get Specializations
#endif

    /** Compares the contents of this field to a string */
    template<>
    CONSTEXPR bool CSVField::operator==(const char * other) const noexcept
    {
        return this->sv == other;
    }

    /** Compares the contents of this field to a string */
    template<>
    CONSTEXPR bool CSVField::operator==(csv::string_view other) const noexcept
    {
        return this->sv == other;
    }
}

inline std::ostream& operator << (std::ostream& os, csv::CSVField const& value) {
    os << std::string(value);
    return os;
}
