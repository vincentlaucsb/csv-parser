/** @file
  *  A standalone header file for writing delimiter-separated files
  */

#pragma once
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "common.hpp"
#include "data_type.h"

namespace csv {
    namespace internals {
        static int DECIMAL_PLACES = 5;

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
                std::string result;

                T integral_part;
                T fractional_part = std::abs(std::modf(value, &integral_part));
                integral_part = std::abs(integral_part);

                // Integral part
                if (value < 0) result = "-";

                if (integral_part == 0) {
                    result = "0";
                }
                else {
                    for (int n_digits = (int)(std::log(integral_part) / std::log(10));
                         n_digits + 1 > 0; n_digits --) {
                        int digit = (int)(std::fmod(integral_part, pow10(n_digits + 1)) / pow10(n_digits));
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
#ifndef __clang___
    inline static void set_decimal_places(int precision) {
        internals::DECIMAL_PLACES = precision;
    }
#endif

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
            : out(_out), quote_minimal(_quote_minimal) {};

        /** Construct a DelimWriter over the file
         *
         *  @param[out] filename  File to write to
         */
        DelimWriter(const std::string& filename) : DelimWriter(std::ifstream(filename)) {};

        /** Destructor will flush remaining data
         *
         */
        ~DelimWriter() {
            out.flush();
        }

        /** Format a sequence of strings and write to CSV according to RFC 4180
         *
         *  @warning This does not check to make sure row lengths are consistent
         *
         *  @param[in]  record          Sequence of strings to be formatted
         *
         *  @return  The current DelimWriter instance (allowing for operator chaining)
         */
        template<typename T, size_t Size>
        DelimWriter& operator<<(const std::array<T, Size>& record) {
            for (size_t i = 0; i < Size; i++) {
                out << csv_escape(record[i]);
                if (i + 1 != Size) out << Delim;
            }

            end_out();
            return *this;
        }

        /** @copydoc operator<< */
        template<typename... T>
        DelimWriter& operator<<(const std::tuple<T...>& record) {
            this->write_tuple<0, T...>(record);
            return *this;
        }

        /**
         * @tparam T A container such as std::vector, std::deque, or std::list
         * 
         * @copydoc operator<<
         */
        template<
            typename T, typename Alloc, template <typename, typename> class Container,

            // Avoid conflicting with tuples with two elements
            csv::enable_if_t<std::is_class<Alloc>::value, int> = 0
        >
            DelimWriter& operator<<(const Container<T, Alloc>& record) {
            const size_t ilen = record.size();
            size_t i = 0;
            for (const auto& field : record) {
                out << csv_escape(field);
                if (i + 1 != ilen) out << Delim;
                i++;
            }

            end_out();
            return *this;
        }

        /** Flushes the written data
         *
         */
        void flush() {
            out.flush();
        }

    private:
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
            
            return _csv_escape(std::string(in));
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
            out << csv_escape(std::get<Index>(record));

            IF_CONSTEXPR (Index + 1 < sizeof...(T)) out << Delim;

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
            out << '\n';
            IF_CONSTEXPR(Flush) out.flush();
        }

        OutputStream & out;
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