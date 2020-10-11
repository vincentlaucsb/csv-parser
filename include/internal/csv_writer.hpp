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

#include "compatibility.hpp"
#include "data_type.h"

namespace csv {
    namespace internals {
        template<typename T,
            std::enable_if_t<std::is_unsigned<T>::value, int> = 0>
        inline std::string to_string(T value) {
            std::string digits_reverse = "";

            while (value > 0) {
                digits_reverse += (char)('0' + (value % 10));
                value /= 10;
            }

            return std::string(digits_reverse.rbegin(), digits_reverse.rend());
        }

        template<typename T,
            std::enable_if_t<std::is_integral<T>::value && std::is_signed<T>::value, int> = 0>
        inline std::string to_string(T value) {
            if (value > 0) return to_string((size_t)value);

            return "-" + to_string((size_t)(value * -1));
        }

        template<typename T,
            std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
        inline std::string to_string(T value) {
            std::string result;

            if (value < 0)
                result = "-";
            
            // Integral part
            size_t integral = (size_t)(std::abs(value));
            result += to_string(integral);

            // Decimal part
            size_t decimal = (size_t)(((double)std::abs(value) - (double)integral) * 100000);

            result += ".";

            if (decimal == 0) {
                result += "0";
            }
            else {
                result += to_string(decimal);
            }

            return result;
        }
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
     *
     *  @par Hint
     *  Use the aliases csv::CSVWriter<OutputStream> to write CSV
     *  formatted strings and csv::TSVWriter<OutputStream>
     *  to write tab separated strings
     *
     *  @par Example
     *  @snippet test_write_csv.cpp CSV Writer Example
     */
    template<class OutputStream, char Delim, char Quote>
    class DelimWriter {
    public:
        /** Construct a DelimWriter over the specified output stream */
        DelimWriter(OutputStream& _out, bool _quote_minimal = true)
            : out(_out), quote_minimal(_quote_minimal) {};

        /** Construct a DelimWriter over the file
         *
         *  @param[out] filename  File to write to
         */
        DelimWriter(const std::string& filename) : DelimWriter(std::ifstream(filename)) {};

        /** Format a sequence of strings and write to CSV according to RFC 4180
         *
         *  @warning This does not check to make sure row lengths are consistent
         *
         *  @param[in]  record          Sequence of strings to be formatted
         *  @param      quote_minimal   Only quote fields if necessary
         *
         *  @return  The current DelimWriter instance (allowing for operator chaining)
         */
        template<typename T, typename Alloc, template <typename, typename> class Container>
        DelimWriter& operator<<(const Container<T, Alloc>& record) {
            const size_t ilen = record.size();
            size_t i = 0;
            for (const auto& field : record) {
                out << csv_escape(field);
                if (i + 1 != ilen) out << Delim;
                i++;
            }

            out << std::endl;
            return *this;
        }

        /** @copydoc write_row
         *  @return  The current DelimWriter instance (allowing for operator chaining)
         */
        template<typename T, size_t Size>
        DelimWriter& operator<<(const std::array<T, Size>& record) {
            for (size_t i = 0; i < Size; i++) {
                out << csv_escape(record[i]);
                if (i + 1 != Size) out << Delim;
            }

            out << std::endl;
            return *this;
        }

        template<typename... T>
        DelimWriter& operator<<(const std::tuple<T...>& record) {
            this->write_tuple<0, T...>(record);
            return *this;
        }

    private:
#ifndef DOXYGEN_SHOULD_SKIP_THIS
        template<typename T,
            std::enable_if_t<!std::is_convertible<T, csv::string_view>::value, int> = 0
        >
        std::string csv_escape(T in) {
            return internals::to_string(in);
        }

        template<typename T,
            std::enable_if_t<std::is_convertible<T, csv::string_view>::value, int> = 0
        >
        std::string csv_escape(T in) {
            return _csv_escape(in);
        }

#endif

        std::string _csv_escape(csv::string_view in) {
            /** Format a string to be RFC 4180-compliant
             *  @param[in]  in              String to be CSV-formatted
             *  @param[out] quote_minimal   Only quote fields if necessary.
             *                              If False, everything is quoted.
             */

            // Do we need a quote escape
            bool quote_escape = false;

            for (auto ch : in) {
                if (ch == Quote || ch == Delim) {
                    quote_escape = true;
                    break;
                }
            }

            if (!quote_escape) {
                if (quote_minimal) return std::string(in);
                else {
                    std::string out(Quote, 1);
                    out += in;
                    out += Quote;
                }
            }

            // Start initial quote escape sequence
            std::string out(1, Quote);
            for (auto ch: in) {
                if (ch == Quote) out += std::string(2, Quote);
                else out += ch;
            }

            // Finish off quote escape
            out += Quote;
            return out;
        }

        template<size_t Index = 0, typename... T>
        typename std::enable_if<Index < sizeof...(T), void>::type write_tuple(const std::tuple<T...>& record) {
            out << csv_escape(std::get<Index>(record));

            IF_CONSTEXPR (Index + 1 < sizeof...(T)) out << Delim;

            this->write_tuple<Index + 1>(record);
        }

        template<size_t Index = 0, typename... T>
        typename std::enable_if<Index == sizeof...(T), void>::type write_tuple(const std::tuple<T...>& record) {
            out << std::endl;
        }

        bool quote_minimal;
        OutputStream & out;
    };

    /** Class for writing CSV files
     *
     *  @sa csv::DelimWriter::write_row()
     *  @sa csv::DelimWriter::operator<<()
     *
     *  @note Use `csv::make_csv_writer()` to in instatiate this class over
     *        an actual output stream.
     */
    template<class OutputStream>
    using CSVWriter = DelimWriter<OutputStream, ',', '"'>;

    /** Class for writing tab-separated values files
*
     *  @sa csv::DelimWriter::write_row()
     *  @sa csv::DelimWriter::operator<<()
     *
     *  @note Use `csv::make_tsv_writer()` to in instatiate this class over
     *        an actual output stream.
     */
    template<class OutputStream>
    using TSVWriter = DelimWriter<OutputStream, '\t', '"'>;

    //
    // Temporary: Until more C++17 compilers support template deduction guides
    //
    template<class OutputStream>
    inline CSVWriter<OutputStream> make_csv_writer(OutputStream& out) {
        /** Return a CSVWriter over the output stream */
        return CSVWriter<OutputStream>(out);
    }

    template<class OutputStream>
    inline TSVWriter<OutputStream> make_tsv_writer(OutputStream& out) {
        /** Return a TSVWriter over the output stream */
        return TSVWriter<OutputStream>(out);
    }

    ///@}
}