/** @file
 *  Defines the data type used for storing information about a CSV row
 */

#pragma once
#include <chrono>
#include <cstdint>
#include <cmath>
#include <iterator>
#include <memory> // For CSVField
#include <limits> // For CSVField
#include <unordered_set>
#include <string>
#include <sstream>
#include <vector>

#include "common.hpp"
#ifdef CSV_HAS_CXX17
#include <optional>
#endif
#ifdef CSV_HAS_CXX23
#if defined(__has_include)
#if __has_include(<expected>)
#include <expected>
#ifdef __cpp_lib_expected
#define CSV_HAS_STD_EXPECTED
#endif
#endif
#endif
#endif
#include "csv_exceptions.hpp"
#ifdef CSV_HAS_CXX20
#include <ranges>
#endif
#include "data_type.hpp"
#include "../external/classify_scalar.hpp"
#include "json_converter.hpp"
#include "raw_csv_data.hpp"

namespace csv {
    namespace internals {
        template<typename RowSink, typename ParsePolicy, typename FieldPolicy, typename RowPolicy>
        class CSVParserCore;
        struct CSVRowRowPolicy;
        namespace parser {
            class CSVParserDriverBase;
        }
        namespace speculative {
            struct CSVRowFragment;
        }

        static const std::string ERROR_NAN = "Not a number.";
        static const std::string ERROR_OVERFLOW = "Overflow error.";
        static const std::string ERROR_FLOAT_TO_INT =
            "Attempted to convert a floating point value to an integral type.";
        static const std::string ERROR_NEG_TO_UNSIGNED = "Negative numbers cannot be converted to unsigned types.";
    
        // Inside CSVField::get() or wherever you materialize the value
        csv::string_view get_trimmed(csv::string_view sv, const WhitespaceMap& ws_flags) noexcept;
    }

    /**
    * @enum CSVConversionError
    * @brief Non-throwing CSVField conversion result.
    *
    * Returned by CSVField::as() inside std::expected, and used internally by
    * CSVField::get() and CSVField::try_get() to keep throwing and non-throwing
    * conversions on the same rules.
    *
    * @sa csv_conversion_error_message()
    */
    enum class CSVConversionError {
        /** Conversion succeeded. */
        None = 0,

        /** The field is not compatible with the requested target type. */
        NotANumber,

        /** The parsed value does not fit in the requested target type. */
        Overflow,

        /** A floating point field was requested as an integral type. */
        FloatToInt,

        /** A negative value was requested as an unsigned type. */
        NegativeToUnsigned
    };

    namespace internals {
        typedef const char* csv_error_message;

        static CONSTEXPR_VALUE_14 csv_error_message CSV_CONVERSION_ERROR_MESSAGES[] = {
            "",
            "Not a number.",
            "Overflow error.",
            "Attempted to convert a floating point value to an integral type.",
            "Negative numbers cannot be converted to unsigned types."
        };
    }

    /** Return a stable human-readable description for a CSVConversionError. */
    inline const char* csv_conversion_error_message(CSVConversionError error) noexcept {
        const size_t index = static_cast<size_t>(error);
        return index < (sizeof(internals::CSV_CONVERSION_ERROR_MESSAGES) / sizeof(internals::CSV_CONVERSION_ERROR_MESSAGES[0]))
            ? internals::CSV_CONVERSION_ERROR_MESSAGES[index]
            : internals::CSV_CONVERSION_ERROR_MESSAGES[static_cast<size_t>(CSVConversionError::NotANumber)];
    }

    /**
    * @class CSVField
    * @brief Data type representing individual CSV values.
    *        CSVFields can be obtained by using CSVRow::operator[]
    */
    class CSVField {
    public:
        /** Constructs a CSVField from a string_view */
        constexpr explicit CSVField(csv::string_view _sv) noexcept
            : sv(_sv.data() ? _sv : csv::string_view("")) {}

        CSVField(csv::string_view _sv, const internals::CSVFieldScalar& scalar) noexcept
            : sv(_sv.data() ? _sv : csv::string_view("")),
              type_(scalar.type) {
            switch (scalar.type) {
            case DataType::CSV_INT8:
            case DataType::CSV_INT16:
            case DataType::CSV_INT32:
            case DataType::CSV_INT64:
                this->value_.integer = scalar.integer;
                break;
            case DataType::CSV_DOUBLE:
            case DataType::CSV_BIGINT:
                this->value_.floating = scalar.floating;
                break;
            case DataType::CSV_TIMESTAMP:
                this->value_.timestamp = scalar.timestamp;
                break;
            case DataType::CSV_BOOL:
                this->value_.boolean = scalar.boolean;
                break;
            default:
                break;
            }
        }

        operator csv::string_view() const noexcept {
            return this->sv;
        }

        operator std::string() const {
            return std::string(this->sv);
        }

        /** Returns the value casted to the requested type, performing type checking before.
        *
        *  \par Valid options for T
        *   - std::string or csv::string_view
        *   - signed integral types (signed char, short, int, long int, long long int)
        *   - unsigned integral types (unsigned char, unsigned short, unsigned int, unsigned long long)
        *   - floating point types (float, double, long double)
        *
        *  \par Invalid conversions
        *   - Converting non-numeric values to any numeric type
        *   - Converting floating point values to integers
        *   - Converting a large integer to a smaller type that will not hold it
        *
        *  @note    This method is capable of parsing scientific E-notation.
        *           See @ref scalar_conversions for more details.
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
            T out{};
            const CSVConversionError err = check_convert(out);
            if (err != CSVConversionError::None) throw std::runtime_error(csv_conversion_error_message(err));
            return out;
        }

#ifdef CSV_HAS_STD_EXPECTED
        /**
         * Return this field as T, preserving conversion failure as CSVConversionError.
         *
         * @return std::expected containing T on success, or CSVConversionError
         *         describing why conversion failed.
         *
         * @note Requires C++23 and a standard library that provides std::expected.
         *
         * @sa CSVConversionError
         * @sa csv_conversion_error_message()
         */
        template<typename T = std::string>
        std::expected<T, CSVConversionError> as() {
            T out{};
            const CSVConversionError err = check_convert(out);
            return (err != CSVConversionError::None)
                ? std::expected<T, CSVConversionError>(std::unexpected(err))
                : std::expected<T, CSVConversionError>(out);
        }
#endif

        /** Non-throwing equivalent of get(). Applies the same type checks and conversions;
         *  returns true and writes to @p out on success, or returns false without throwing.
         *
         *  @sa get() for the full description of valid types, conversion rules, and warnings.
         *
         *  Example:
         *  @code
         *  int value;
         *  if (field.try_get(value)) {
         *      // Use value safely
         *  } else {
         *      // Handle conversion failure
         *  }
         *  @endcode
         */
        template<typename T = std::string>
        bool try_get(T& out) noexcept {
            return check_convert(out) == CSVConversionError::None;
        }

#ifdef CSV_HAS_CXX17
        /** @anchor CSVField_optional_conversion
         *  Convert this field to std::optional<T>, returning std::nullopt when conversion fails.
         *
         *  This is a value-returning wrapper around try_get(), useful for C++17
         *  callers that want non-throwing conversion without an output parameter.
         *
         *  @note Requires C++17 or later.
         */
        template<typename T>
        operator std::optional<T>() {
            T out{};
            return try_get(out) ? std::optional<T>(out) : std::nullopt;
        }
#endif

        /** Parse a hexadecimal value, returning false if the value is not hex.
         *  @tparam T An integral type (int, long, long long, etc.)
         */
        template<typename T = long long>
        bool try_parse_hex(T& parsedValue) {
            static_assert(std::is_integral<T>::value,
                "try_parse_hex only works with integral types (int, long, long long, etc.)");

            return classify_scalar::parse_hex(this->sv.data(), this->sv.data() + this->sv.size(), parsedValue);
        }

        /** Attempts to parse a decimal (or integer) value using the given symbol,
         *  returning `true` if the value is numeric.
         *
         *  @note This method also updates this field's type
         *
         */
        bool try_parse_decimal(long double& dVal, const char decimalSymbol = '.');

        /** Parse this field as Unix milliseconds.
         *
         *  Timestamp-classified values return their parsed epoch value. Integral
         *  values are treated as already being Unix milliseconds.
         */
        bool try_parse_timestamp(std::uint64_t& out) noexcept;

        /** Parse this field as Unix milliseconds in a 64-bit unsigned integer. */
#ifdef DOXYGEN_SHOULD_SKIP_THIS
        template<typename T>
        bool try_parse_timestamp(T& out) noexcept;
#else
        template<typename T>
        internals::enable_if_t<
            std::is_integral<T>::value && std::is_unsigned<T>::value && !std::is_same<T, bool>::value
            && (sizeof(T) >= sizeof(std::uint64_t)),
            bool
        >
        try_parse_timestamp(T& out) noexcept {
            std::uint64_t milliseconds = 0;
            if (!this->try_parse_timestamp(milliseconds))
                return false;

            out = static_cast<T>(milliseconds);
            return true;
        }
#endif

        /** Parse this field as a timestamp duration since the Unix epoch. */
        template<typename Rep, typename Period>
        bool try_parse_timestamp(std::chrono::duration<Rep, Period>& out) noexcept {
            std::uint64_t milliseconds = 0;
            if (!this->try_parse_timestamp(milliseconds))
                return false;

            out = std::chrono::duration_cast<std::chrono::duration<Rep, Period>>(
                std::chrono::milliseconds(milliseconds));
            return true;
        }

        /** Parse this field as a std::chrono::system_clock time point. */
        template<typename Duration>
        bool try_parse_timestamp(std::chrono::time_point<std::chrono::system_clock, Duration>& out) noexcept {
            std::uint64_t milliseconds = 0;
            if (!this->try_parse_timestamp(milliseconds))
                return false;

            out = std::chrono::time_point<std::chrono::system_clock, Duration>(
                std::chrono::duration_cast<Duration>(std::chrono::milliseconds(milliseconds)));
            return true;
        }

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
        inline bool operator==(T other) const noexcept
        {
            static_assert(std::is_arithmetic<T>::value,
                "T should be a numeric value.");

            const_cast<CSVField*>(this)->get_value();
            if (this->type_ < DataType::CSV_INT8 || this->type_ > DataType::CSV_DOUBLE || this->type_ == DataType::CSV_BIGINT) {
                return false;
            }

            return internals::is_equal(this->numeric_value_as_long_double(), static_cast<long double>(other), 0.000001L);
        }

        /** Return a string view over the field's contents */
        CONSTEXPR csv::string_view get_sv() const noexcept { return this->sv; }

        /** Returns true if field is an empty string or string of whitespace characters */
        inline bool is_null() noexcept { return type() == DataType::CSV_NULL; }

        /** Returns true if field is a non-numeric, non-empty string */
        inline bool is_str() noexcept { return type() == DataType::CSV_STRING; }

        /** Returns true if field is an integer or float */
        inline bool is_num() noexcept {
            return type() >= DataType::CSV_INT8 && type() <= DataType::CSV_DOUBLE;
        }

        /** Returns true if field is an integer */
        inline bool is_int() noexcept {
            return (type() >= DataType::CSV_INT8) && (type() <= DataType::CSV_INT64);
        }

        /** Returns true if field is a floating point value */
        inline bool is_float() noexcept { return type() == DataType::CSV_DOUBLE; }

        /** Returns true if field is a boolean value */
        inline bool is_bool() noexcept { return type() == DataType::CSV_BOOL; }

        /** Returns true if field is a timestamp value */
        inline bool is_timestamp() noexcept { return type() == DataType::CSV_TIMESTAMP; }

        /** Return the type of the underlying CSV data */
        inline DataType type() noexcept {
            this->get_value();
            return type_;
        }

    private:
        // GCC emits a psABI note for by-value APIs involving unions with long double.
        // This is a workaround to keep the logs clean without sacrificing the benefits of a union on other compilers.
        // Give only GCC users the struct tax so normal builds and strict CI logs stay quiet.
#if defined(__GNUC__) && !defined(__clang__)
        struct FieldValue {
            constexpr FieldValue() noexcept
                : integer(0), floating(0), timestamp(0), boolean(false) {}

            std::int64_t integer;
            long double floating;
            std::uint64_t timestamp;
            bool boolean;
        };
#else
        union FieldValue {
            constexpr FieldValue() noexcept : floating(0) {}

            std::int64_t integer;
            long double floating;
            std::uint64_t timestamp;
            bool boolean;
        };
#endif

        struct FieldValueOutput {
            FieldValue& value;

            template<classify_scalar::ScalarKind Kind>
            typename std::enable_if<
                Kind == classify_scalar::scalar_int8
                || Kind == classify_scalar::scalar_int16
                || Kind == classify_scalar::scalar_int32
                || Kind == classify_scalar::scalar_int64,
                void>::type set(std::int64_t parsed) const noexcept {
                value.integer = parsed;
            }

            template<classify_scalar::ScalarKind Kind>
            typename std::enable_if<Kind == classify_scalar::scalar_float, void>::type set(long double parsed) const noexcept {
                value.floating = parsed;
            }

            template<classify_scalar::ScalarKind Kind>
            typename std::enable_if<Kind == classify_scalar::scalar_bool, void>::type set(bool parsed) const noexcept {
                value.boolean = parsed;
            }

            template<classify_scalar::ScalarKind Kind>
            typename std::enable_if<Kind == classify_scalar::scalar_timestamp, void>::type set(std::uint64_t parsed) const noexcept {
                value.timestamp = parsed;
            }
        };

        FieldValue value_;        /**< Cached typed values. */
        csv::string_view sv = ""; /**< A pointer to this field's text */
        DataType type_ = DataType::UNKNOWN; /**< Cached data type value */

        CONSTEXPR_14 bool stores_integral() const noexcept {
            return type_ >= DataType::CSV_INT8 && type_ <= DataType::CSV_INT64;
        }

        CONSTEXPR_14 long double numeric_value_as_long_double() const noexcept {
            return stores_integral()
                ? static_cast<long double>(value_.integer)
                : value_.floating;
        }

        CONSTEXPR_14 void cache_parsed_value(DataType parsed_type, long double parsed_value) noexcept {
            type_ = parsed_type;

            if (parsed_type >= DataType::CSV_INT8 && parsed_type <= DataType::CSV_INT64) {
                value_.integer = static_cast<std::int64_t>(parsed_value);
            }
            else if (parsed_type == DataType::CSV_DOUBLE || parsed_type == DataType::CSV_BIGINT) {
                value_.floating = parsed_value;
            }
        }

        /** Shared validation + conversion kernel used by get(), try_get(), and as().
         *  Assigns to @p out and returns CSVConversionError::None on success.
         */
        CSVConversionError check_convert(bool& out) noexcept {
            if (this->type() != DataType::CSV_BOOL)
                return CSVConversionError::NotANumber;

            out = this->value_.boolean;
            return CSVConversionError::None;
        }

        template<typename Rep, typename Period>
        CSVConversionError check_convert(std::chrono::duration<Rep, Period>& out) noexcept {
            if (this->type() != DataType::CSV_TIMESTAMP)
                return CSVConversionError::NotANumber;

            out = std::chrono::duration_cast<std::chrono::duration<Rep, Period>>(
                std::chrono::milliseconds(this->value_.timestamp));
            return CSVConversionError::None;
        }

        template<typename Duration>
        CSVConversionError check_convert(std::chrono::time_point<std::chrono::system_clock, Duration>& out) noexcept {
            if (this->type() != DataType::CSV_TIMESTAMP)
                return CSVConversionError::NotANumber;

            out = std::chrono::time_point<std::chrono::system_clock, Duration>(
                std::chrono::duration_cast<Duration>(std::chrono::milliseconds(this->value_.timestamp)));
            return CSVConversionError::None;
        }

        template<typename T>
        CSVConversionError check_convert(T& out) noexcept {
            IF_CONSTEXPR(std::is_arithmetic<T>::value) {
                if (!this->is_num())
                    return CSVConversionError::NotANumber;
                if (this->type_ == DataType::CSV_BIGINT)
                    return CSVConversionError::Overflow;
            }

            IF_CONSTEXPR(std::is_integral<T>::value) {
                if (this->is_float())
                    return CSVConversionError::FloatToInt;

                IF_CONSTEXPR(std::is_unsigned<T>::value) {
                    if (this->numeric_value_as_long_double() < 0)
                        return CSVConversionError::NegativeToUnsigned;
                }
            }

            IF_CONSTEXPR(!std::is_floating_point<T>::value) {
                const long double value = this->numeric_value_as_long_double();
                if (value < static_cast<long double>(std::numeric_limits<T>::min())
                    || value > static_cast<long double>(std::numeric_limits<T>::max())) {
                    return CSVConversionError::Overflow;
                }
            }

            out = this->stores_integral()
                ? static_cast<T>(this->value_.integer)
                : static_cast<T>(this->value_.floating);
            return CSVConversionError::None;
        }

        /** Check to see if value has been cached previously before evaluating. */
        inline void get_value() noexcept {
            if ((int)type_ < 0) {
                if (this->sv.empty()) {
                    type_ = DataType::CSV_NULL;
                    return;
                }

                const char* first = this->sv.data();
                const char* last = first + this->sv.size();
                typedef classify_scalar::policy_pack<
                    classify_scalar::builtin_numeric_policy<'.', false>,
                    classify_scalar::builtin_timestamp_policy,
                    classify_scalar::builtin_bool_policy
                > csv_field_policy_pack;

                type_ = classify_scalar::classify_scalar<
                    DataType,
                    true>(first, last, FieldValueOutput{ this->value_ }, csv_field_policy_pack());
            }
        }
    };

    /** Data structure for representing CSV rows */
    class CSVRow {
    public:
        template<typename RowSink, typename ParsePolicy, typename FieldPolicy, typename RowPolicy>
        friend class internals::CSVParserCore;
        friend struct internals::CSVRowRowPolicy;
        friend internals::parser::CSVParserDriverBase;
        friend struct internals::speculative::CSVRowFragment;

        CSVRow() = default;
        
        /** Construct a CSVRow view over parsed row storage. */
        CSVRow(internals::RawCSVDataPtr _data) : data(_data) {}
        CSVRow(internals::RawCSVDataPtr _data, size_t _data_start, size_t _field_bounds)
            : data(_data), data_start(_data_start), fields_start(_field_bounds) {}
        CSVRow(internals::RawCSVDataPtr _data, size_t _data_start, size_t _field_bounds, size_t _row_length)
            : data(_data), data_start(_data_start), fields_start(_field_bounds), row_length(_row_length) {}

        /** Indicates whether row is empty or not */
        CONSTEXPR bool empty() const noexcept { return this->size() == 0; }

        /** Return the number of fields in this row */
        CONSTEXPR size_t size() const noexcept { return row_length; }

        /** @name Value Retrieval */
        ///@{
        CSVField operator[](size_t n) const;
        CSVField operator[](csv::string_view) const;
        inline std::string to_json(const std::vector<std::string>& subset = {}) const {
            const auto* converter = this->get_json_converter();
            return converter == nullptr ? "{}"
                : converter->row_to_json(this->size(), [this](size_t i) { return this->get_field(i); }, subset);
        }
        inline std::string to_json_array(const std::vector<std::string>& subset = {}) const {
            const auto* converter = this->get_json_converter();
            return converter == nullptr ? "[]"
                : converter->row_to_json_array(this->size(), [this](size_t i) { return this->get_field(i); }, subset);
        }

        /** Retrieve this row's associated column names */
        const std::vector<std::string>& get_col_names() const {
            return this->data->col_names->get_col_names();
        }

        /** Internal accessor for preserving resolved column-name lookup policy across helper types. */
        internals::ConstColNamesPtr col_names_ptr() const noexcept {
            return this->data->col_names;
        }

        /** Convert this CSVRow into an unordered map.
         *  The keys are the column names and the values are the corresponding field values.
         */
        std::unordered_map<std::string, std::string> to_unordered_map() const;

        /** Convert a selected subset of columns into an unordered map. */
        std::unordered_map<std::string, std::string> to_unordered_map(
            const std::vector<std::string>& subset
        ) const;

        #ifdef CSV_HAS_CXX20
        /** Convert this CSVRow into a std::ranges::input_range of string_views.
         *
         *  @note Requires C++20 or later.
         */
        auto to_sv_range() const {
            return std::views::iota(size_t{0}, this->size())
                | std::views::transform([this](size_t i) { return this->get_field(i); });
        }
        #endif

        /** Convert this row into a `std::vector<std::string>`.
         *
         * This conversion is primarily intended for write-side workflows, such as
         * reordering or selecting columns before forwarding the row to `CSVWriter`.
         *
         * @note This is less efficient than indexed access via `operator[]` because
         *       it materializes all fields as owning strings.
         */
        operator std::vector<std::string>() const;

        /** Return a string_view of the raw bytes of this row as they appear in
         *  the underlying parse buffer, up to (but not including) the trailing
         *  record terminator.
         *
         *  @warning The view is only valid for as long as the CSVRow (and its
         *           associated data chunk) remains alive.
         */
        csv::string_view raw_str() const noexcept;
        ///@}

        /** A random access iterator over the contents of a CSV row.
         *  Each iterator points to a CSVField.
         */
        class iterator {
        public:
#ifndef DOXYGEN_SHOULD_SKIP_THIS
            using value_type = CSVField;
            using difference_type = int;
            using pointer = std::shared_ptr<CSVField>;
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
            iterator& operator+=(difference_type n);
            iterator& operator-=(difference_type n);
            difference_type operator-(const iterator& other) const noexcept;

            /** Two iterators are equal if they point to the same field */
            CONSTEXPR bool operator==(const iterator& other) const noexcept {
                return this->i == other.i;
            };

            CONSTEXPR bool operator!=(const iterator& other) const noexcept { return !operator==(other); }

#ifndef NDEBUG
            friend CSVRow;
#endif

        private:
            const CSVRow * daddy = nullptr;                      // Pointer to parent
            internals::RawCSVDataPtr data = nullptr;             // Keep data alive for lifetime of iterator
            std::shared_ptr<CSVField> field = nullptr;           // Current field pointed at
            int i = 0;                                           // Index of current field
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
        /** Shared implementation for field access (handles quoting and caching). */
        inline csv::string_view get_field_impl(size_t index, const internals::RawCSVDataPtr& _data) const {
            if (index >= this->size())
                throw std::runtime_error(internals::CSV_ERROR_INDEX_OUT_OF_BOUNDS);

            const size_t field_index = this->fields_start + index;
            const auto field = _data->fields[field_index];
            csv::string_view field_str;
            if (field.has_realized_storage()) {
                field_str = _data->quote_arena.view(field.start, field.length);
            }
            else {
                field_str = csv::string_view(_data->data).substr(this->data_start + field.start, field.length);
            }

            if (_data->has_ws_trimming) {
                field_str = internals::get_trimmed(field_str, _data->ws_flags);
            }

            return field_str;
        }

        CSVField make_field(size_t index, const internals::RawCSVDataPtr& _data) const;

        /** Retrieve a string view corresponding to the specified index */
        csv::string_view get_field(size_t index) const;

        /** Iterator-safe field access using explicit data pointer 
         *  (prevents accessing freed data when CSVRow is reassigned)
         */
        csv::string_view get_field_safe(size_t index, internals::RawCSVDataPtr _data) const;

        const internals::JsonConverter* get_json_converter() const {
            if (this->data.get() == nullptr) {
                return nullptr;
            }

            return &this->data->json_converter.get_or_create([this]() {
                const std::vector<std::string> columns = this->data->col_names
                    ? this->data->col_names->get_col_names()
                    : std::vector<std::string>();
                return std::make_shared<internals::JsonConverter>(columns);
            });
        }

        internals::RawCSVDataPtr data;

        /** Byte offset where this row begins within the shared row storage. */
        size_t data_start = 0;

        /** Field-list offset where this row begins. */
        size_t fields_start = 0;

        /** How many columns this row spans */
        size_t row_length = 0;

        /** Byte offset one past the last byte belonging to this row. */
        size_t data_end = (std::numeric_limits<size_t>::max)();
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
    inline long double CSVField::get<long double>() {
        if (!is_num())
            throw std::runtime_error(internals::ERROR_NAN);

        return this->numeric_value_as_long_double();
    }

    /** Non-throwing retrieval of field as std::string */
    template<>
    inline bool CSVField::try_get<std::string>(std::string& out) noexcept {
        out = std::string(this->sv);
        return true;
    }

    /** Non-throwing retrieval of field as csv::string_view */
    template<>
    CONSTEXPR_14 bool CSVField::try_get<csv::string_view>(csv::string_view& out) noexcept {
        out = this->sv;
        return true;
    }

    /** Non-throwing retrieval of field as long double */
    template<>
    inline bool CSVField::try_get<long double>(long double& out) noexcept {
        if (!is_num())
            return false;

        out = this->numeric_value_as_long_double();
        return true;
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
