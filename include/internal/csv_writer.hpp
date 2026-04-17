/** @file
  *  A standalone header file for writing delimiter-separated files
  */

#pragma once
#include <fstream>
#include <iostream>
#include <memory>
#ifdef CSV_HAS_CXX20
    #include <ranges>
#endif
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "common.hpp"
#include "data_type.hpp"

namespace csv {
    namespace internals {
        static int DECIMAL_PLACES = 5;

        /**
         * Calculate the absolute value of a number
         */
        template<typename T = int>
        inline T csv_abs(T x) {
            return abs(x);
        }

        template<>
        inline int csv_abs(int x) {
            return abs(x);
        }

        template<>
        inline long int csv_abs(long int x) {
            return labs(x);
        }

        template<>
        inline long long int csv_abs(long long int x) {
            return llabs(x);
        }

        template<>
        inline float csv_abs(float x) {
            return fabsf(x);
        }

        template<>
        inline double csv_abs(double x) {
            return fabs(x);
        }

        template<>
        inline long double csv_abs(long double x) {
            return fabsl(x);
        }

        /** 
         * Calculate the number of digits in a number
         */
        template<
            typename T,
            csv::enable_if_t<std::is_arithmetic<T>::value, int> = 0
        >
        int num_digits(T x)
        {
            x = csv_abs(x);

            int digits = 0;

            while (x >= 1) {
                x /= 10;
                digits++;
            }

            return digits;
        }

        /** to_string() for unsigned integers */
        template<typename T,
            csv::enable_if_t<std::is_unsigned<T>::value, int> = 0>
        inline std::string to_string(T value) {
            std::string digits_reverse = "";

            if (value == 0) return "0";

            while (value > 0) {
                digits_reverse += (char)('0' + (value % 10));
                value /= 10;
            }

            return std::string(digits_reverse.rbegin(), digits_reverse.rend());
        }

        /** to_string() for signed integers */
        template<
            typename T,
            csv::enable_if_t<std::is_integral<T>::value && std::is_signed<T>::value, int> = 0
        >
        inline std::string to_string(T value) {
            if (value >= 0)
                return to_string((size_t)value);

            return "-" + to_string((size_t)(value * -1));
        }

        /** to_string() for floating point numbers */
        template<
            typename T,
            csv::enable_if_t<std::is_floating_point<T>::value, int> = 0
        >
            inline std::string to_string(T value) {
#ifdef __clang__
            return std::to_string(value);
#else
            // TODO: Figure out why the below code doesn't work on clang
                std::string result = "";

                T integral_part;
                T fractional_part = std::abs(std::modf(value, &integral_part));
                integral_part = std::abs(integral_part);

                // Integral part
                if (value < 0) result = "-";

                if (integral_part == 0) {
                    result += "0";
                }
                else {
                    for (int n_digits = num_digits(integral_part); n_digits > 0; n_digits --) {
                        int digit = (int)(std::fmod(integral_part, pow10(n_digits)) / pow10(n_digits - 1));
                        result += (char)('0' + digit);
                    }
                }

                // Decimal part
                result += ".";

                if (fractional_part > 0) {
                    fractional_part *= (T)(pow10(DECIMAL_PLACES));
                    for (int n_digits = DECIMAL_PLACES; n_digits > 0; n_digits--) {
                        int digit = (int)(std::fmod(fractional_part, pow10(n_digits)) / pow10(n_digits - 1));
                        result += (char)('0' + digit);
                    }
                }
                else {
                    result += "0";
                }

                return result;
#endif
        }
    }

    /** Sets how many places after the decimal will be written for floating point numbers
     *
     *  @param  precision   Number of decimal places
     */
#ifndef __clang__
    inline static void set_decimal_places(int precision) {
        internals::DECIMAL_PLACES = precision;
    }
#endif

    namespace internals {
        /** SFINAE trait: detects if a type is iterable (has std::begin/end). */
        template<typename T, typename = void>
        struct is_iterable : std::false_type {};

        template<typename T>
        struct is_iterable<T, typename std::enable_if<true>::type> {
        private:
            template<typename U>
            static auto test(int) -> decltype(
                std::begin(std::declval<const U&>()),
                std::end(std::declval<const U&>()),
                std::true_type{}
            );
            template<typename>
            static std::false_type test(...);
        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        /** SFINAE trait: detects if a type is a std::tuple. */
        template<typename T, typename = void>
        struct is_tuple : std::false_type {};

        template<typename T>
        struct is_tuple<T, typename std::enable_if<true>::type> {
        private:
            template<typename U>
            static auto test(int) -> decltype(std::tuple_size<U>::value, std::true_type{});
            template<typename>
            static std::false_type test(...);
        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };
    }

    /** @name CSV Writing */
    ///@{
    /** 
     *  Class for writing delimiter separated values files
     *
     *  To write formatted strings, one should
     *   -# Initialize a DelimWriter with respect to some output stream 
     *   -# Call write_row() on std::vector<std::string>s of unformatted text
     *
     *  @tparam OutputStream The output stream, e.g. `std::ofstream`, `std::stringstream`
     *  @tparam Delim        The delimiter character
     *  @tparam Quote        The quote character
     *  @tparam Flush        True: flush after every writing function,
     *                       false: you need to flush explicitly if needed.
     *                       In both cases the destructor will flush.
     *
     *  @par Hint
     *  Use the aliases csv::CSVWriter<OutputStream> to write CSV
     *  formatted strings and csv::TSVWriter<OutputStream>
     *  to write tab separated strings
     *
     *  @par Example w/ std::vector, std::deque, std::list
     *  @snippet test_write_csv.cpp CSV Writer Example
     *
     *  @par Example w/ std::tuple
     *  @snippet test_write_csv.cpp CSV Writer Tuple Example
     */
    template<class OutputStream, char Delim, char Quote, bool Flush>
    class DelimWriter {
    public:
        /** Construct a DelimWriter over the specified output stream
         *
         *  @param  _out           Stream to write to
         *  @param  _quote_minimal Limit field quoting to only when necessary
        */

        DelimWriter(OutputStream& _out, bool _quote_minimal = true)
            : out(&_out), quote_minimal(_quote_minimal) {}

        /** Construct a DelimWriter over the file
         *
         *  @param[out] filename  File to write to
         */
        template<typename T = OutputStream,
            csv::enable_if_t<std::is_same<T, std::ofstream>::value, int> = 0>
        DelimWriter(const std::string& filename, bool _quote_minimal = true)
            : owned_out(new std::ofstream(filename, std::ios::out)),
            out(owned_out.get()),
            quote_minimal(_quote_minimal) {
            if (!owned_out->is_open())
                throw std::runtime_error("Failed to open file for writing: " + filename);
        }

        /** Destructor will flush remaining data
         *
         */
        ~DelimWriter() {
            out->flush();
        }

        /** Write a C-style array of strings as one delimited row.
         *
         *  @tparam T      Element type (typically std::string or csv::string_view)
         *  @tparam N      Array size (deduced)
         *  @param record  Array of strings
         *  @return        The current DelimWriter instance
         */
        template<typename T, size_t N>
        DelimWriter& operator<<(const T (&record)[N]) {
            write_range_impl(record);
            return *this;
        }

        /** Write a std::array of strings as one delimited row.
         *
         *  @tparam T      Element type (typically std::string or csv::string_view)
         *  @tparam N      Array size (deduced)
         *  @param record  std::array of strings
         *  @return        The current DelimWriter instance
         */
        template<typename T, size_t N>
        DelimWriter& operator<<(const std::array<T, N>& record) {
            write_range_impl(record);
            return *this;
        }

        #ifdef CSV_HAS_CXX20
        /** Write a range of string-like fields as one delimited row.
         *
         *  Accepts any input_range whose elements are convertible to csv::string_view.
         *  This includes std::vector<std::string>, std::vector<csv::string_view>,
         *  std::array, C++20 views, etc.
         */
        template<std::ranges::input_range Range>
        DelimWriter& operator<<(Range&& container)
            requires std::ranges::input_range<Range>
                && std::convertible_to<std::ranges::range_reference_t<Range>, csv::string_view> {
            write_range_impl(container);
            return *this;
        }
        #else
        /** Write a range of string-like fields as one delimited row.
         *
         *  Accepts any input_range whose elements are convertible to csv::string_view.
         *  This includes std::vector<std::string>, std::vector<csv::string_view>,
         *  std::array, C++20 views, etc.
         */
        template<typename Range>
        typename std::enable_if<
            internals::is_iterable<Range>::value 
            && !internals::is_tuple<Range>::value
            && !std::is_same<Range, std::string>::value
            && !std::is_same<Range, csv::string_view>::value,
            DelimWriter&
        >::type operator<<(const Range& record) {
            write_range_impl(record);
            return *this;
        }
#endif

        /** @copydoc operator<< */
        template<typename... T>
        DelimWriter& operator<<(const std::tuple<T...>& record) {
            this->write_tuple<0, T...>(record);
            return *this;
        }

        /** Flushes the written data
         *
         */
        void flush() {
            out->flush();
        }

    private:
        /** Helper to write a range of values, handling first element undelimited,
         *  rest prefixed with delimiter. Inlines aggressively across both C++20 and
         *  C++11 operator<< entry points.
         */
        template<typename Range>
        inline void write_range_impl(const Range& record) {
            auto it = std::begin(record);
            auto end = std::end(record);

            if (it != end) {
                (*out) << csv_escape(*it);
                ++it;
            }

            for (; it != end; ++it) {
                (*out) << Delim << csv_escape(*it);
            }

            end_out();
        }

        template<
            typename T,
            csv::enable_if_t<
                !std::is_convertible<T, std::string>::value
                && !std::is_convertible<T, csv::string_view>::value
            , int> = 0
        >
        std::string csv_escape(T in) {
            return internals::to_string(in);
        }

        template<
            typename T,
            csv::enable_if_t<
                std::is_convertible<T, std::string>::value
                || std::is_convertible<T, csv::string_view>::value
            , int> = 0
        >
        std::string csv_escape(T in) {
            IF_CONSTEXPR(std::is_convertible<T, csv::string_view>::value) {
                return _csv_escape(in);
            }
            else {
                return _csv_escape(std::string(in));
            }
        }

        std::string _csv_escape(csv::string_view in) {
            /** Format a string to be RFC 4180-compliant
             *  @param[in]  in              String to be CSV-formatted
             *  @param[out] quote_minimal   Only quote fields if necessary.
             *                              If False, everything is quoted.
             */

            // Do we need a quote escape
            bool quote_escape = false;

            for (auto ch : in) {
                if (ch == Quote || ch == Delim || ch == '\r' || ch == '\n') {
                    quote_escape = true;
                    break;
                }
            }

            if (!quote_escape) {
                if (quote_minimal) return std::string(in);
                else {
                    std::string ret(1, Quote);
                    ret += in.data();
                    ret += Quote;
                    return ret;
                }
            }

            // Start initial quote escape sequence
            std::string ret(1, Quote);
            for (auto ch: in) {
                if (ch == Quote) ret += std::string(2, Quote);
                else ret += ch;
            }

            // Finish off quote escape
            ret += Quote;
            return ret;
        }

        /** Recurisve template for writing std::tuples */
        template<size_t Index = 0, typename... T>
        typename std::enable_if<Index < sizeof...(T), void>::type write_tuple(const std::tuple<T...>& record) {
            (*out) << csv_escape(std::get<Index>(record));

            IF_CONSTEXPR (Index + 1 < sizeof...(T)) (*out) << Delim;

            this->write_tuple<Index + 1>(record);
        }

        /** Base case for writing std::tuples */
        template<size_t Index = 0, typename... T>
        typename std::enable_if<Index == sizeof...(T), void>::type write_tuple(const std::tuple<T...>& record) {
            (void)record;
            end_out();
        }

        /** Ends a line in 'out' and flushes, if Flush is true.*/
        void end_out() {
            (*out) << '\n';
            IF_CONSTEXPR(Flush) out->flush();
        }

        /**
         * An owned output stream, if the writer owns it.
         * May be null if the writer does not own its output stream, i.e.
         * if it was initialized with an output stream reference instead of a filename.
         */
        std::unique_ptr<OutputStream> owned_out;

        /** Pointer to the output stream (which may or may not be owned by this writer). */
        OutputStream* out;

        bool quote_minimal;
    };

    /** An alias for csv::DelimWriter for writing standard CSV files
     *
     *  @sa csv::DelimWriter::operator<<()
     *
     *  @note Use `csv::make_csv_writer()` to in instatiate this class over
     *        an actual output stream.
     */
    template<class OutputStream, bool Flush = true>
    using CSVWriter = DelimWriter<OutputStream, ',', '"', Flush>;

    /** Class for writing tab-separated values files
    *
     *  @sa csv::DelimWriter::write_row()
     *  @sa csv::DelimWriter::operator<<()
     *
     *  @note Use `csv::make_tsv_writer()` to in instatiate this class over
     *        an actual output stream.
     */
    template<class OutputStream, bool Flush = true>
    using TSVWriter = DelimWriter<OutputStream, '\t', '"', Flush>;

    /** Return a csv::CSVWriter over the output stream */
    template<class OutputStream>
    inline CSVWriter<OutputStream> make_csv_writer(OutputStream& out, bool quote_minimal=true) {
        return CSVWriter<OutputStream>(out, quote_minimal);
    }

    /** Return a buffered csv::CSVWriter over the output stream (does not auto flush) */
    template<class OutputStream>
    inline CSVWriter<OutputStream, false> make_csv_writer_buffered(OutputStream& out, bool quote_minimal=true) {
        return CSVWriter<OutputStream, false>(out, quote_minimal);
    }

    /** Return a csv::TSVWriter over the output stream */
    template<class OutputStream>
    inline TSVWriter<OutputStream> make_tsv_writer(OutputStream& out, bool quote_minimal=true) {
        return TSVWriter<OutputStream>(out, quote_minimal);
    }

    /** Return a buffered csv::TSVWriter over the output stream (does not auto flush) */
    template<class OutputStream>
    inline TSVWriter<OutputStream, false> make_tsv_writer_buffered(OutputStream& out, bool quote_minimal=true) {
        return TSVWriter<OutputStream, false>(out, quote_minimal);
    }
    ///@}
}