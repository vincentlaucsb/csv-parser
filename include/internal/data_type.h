#pragma once
#include <math.h>
#include <cctype>
#include <string>
#include <cassert>

#include "compatibility.hpp"

namespace csv {
    /** Enumerates the different CSV field types that are
    *  recognized by this library
    *
    *  - 0. CSV_NULL (empty string)
    *  - 1. CSV_STRING
    *  - 2. CSV_INT
    *  - 3. CSV_LONG_INT
    *  - 4. CSV_LONG_LONG_INT
    *  - 5. CSV_DOUBLE
    *
    *  **Note**: Overflowing integers will be stored and classified as doubles.
    *  Furthermore, the same number may either be a CSV_LONG_INT or CSV_INT depending on
    *  compiler and platform.
    */
    enum DataType {
        CSV_NULL,
        CSV_STRING,
        CSV_INT,
        CSV_LONG_INT,
        CSV_LONG_LONG_INT,
        CSV_DOUBLE
    };

    namespace internals {
        template<typename T>
        DataType type_num();

        template<> inline DataType type_num<int>() { return CSV_INT; }
        template<> inline DataType type_num<long int>() { return CSV_LONG_INT; }
        template<> inline DataType type_num<long long int>() { return CSV_LONG_LONG_INT; }
        template<> inline DataType type_num<double>() { return CSV_DOUBLE; }
        template<> inline DataType type_num<long double>() { return CSV_DOUBLE; }
        template<> inline DataType type_num<std::nullptr_t>() { return CSV_NULL; }
        template<> inline DataType type_num<std::string>() { return CSV_STRING; }

        /* Compute 10 to the power of n */
        template<typename T>
        const long double pow10(const T& n) {
            long double multiplicand = n > 0 ? 10 : 0.1,
                ret = 1;
            T iterations = n > 0 ? n : -n;
            
            for (T i = 0; i < iterations; i++) {
                ret *= multiplicand;
            }

            return ret;
        }

#ifndef DOXYGEN_SHOULD_SKIP_THIS
        inline std::string type_name(const DataType& dtype) {
            switch (dtype) {
            case CSV_STRING:
                return "string";
            case CSV_INT:
                return "int";
            case CSV_LONG_INT:
                return "long int";
            case CSV_LONG_LONG_INT:
                return "long long int";
            case CSV_DOUBLE:
                return "double";
            default:
                return "null";
            }
        };
#endif

        constexpr long double _INT_MAX = (long double)std::numeric_limits<int>::max();
        constexpr long double _LONG_MAX = (long double)std::numeric_limits<long int>::max();
        constexpr long double _LONG_LONG_MAX = (long double)std::numeric_limits<long long int>::max();

        constexpr DataType data_type(csv::string_view in, long double* const out = nullptr);

        /** Given a pointer to the start of what is start of
         *  the exponential part of a number written (possibly) in scientific notation
         *  parse the exponent
         */
        constexpr inline DataType _process_potential_exponential(
            csv::string_view exponential_part,
            const long double& coeff,
            long double * const out) {
            long double exponent = 0;
            auto result = data_type(exponential_part, &exponent);

            if (result >= CSV_INT && result <= CSV_DOUBLE) {
                if (out) *out = coeff * pow10(exponent);
                return CSV_DOUBLE;
            }

            return CSV_STRING;
        }

        /** Given the absolute value of an integer, determine what numeric type
         *  it fits in
         */
        constexpr DataType _determine_integral_type(const long double& number) {
            // We can assume number is always non-negative
            assert(number >= 0);

            if (number < _INT_MAX)
                return CSV_INT;
            else if (number < _LONG_MAX)
                return CSV_LONG_INT;
            else if (number < _LONG_LONG_MAX)
                return CSV_LONG_LONG_INT;
            else // Conversion to long long will cause an overflow
                return CSV_DOUBLE;
        }

        constexpr DataType data_type(csv::string_view in, long double* const out) {
            /** Distinguishes numeric from other text values. Used by various
             *  type casting functions, like csv_parser::CSVReader::read_row()
             *
             *  #### Rules
             *   - Leading and trailing whitespace ("padding") ignored
             *   - A string of just whitespace is NULL
             *
             *  @param[in] in String value to be examined
             */

             // Empty string --> NULL
            if (in.size() == 0)
                return CSV_NULL;

            bool ws_allowed = true,
                neg_allowed = true,
                dot_allowed = true,
                digit_allowed = true,
                has_digit = false,
                prob_float = false;

            unsigned places_after_decimal = 0;
            long double integral_part = 0,
                decimal_part = 0;

            for (size_t i = 0, ilen = in.size(); i < ilen; i++) {
                const char& current = in[i];

                switch (current) {
                case ' ':
                    if (!ws_allowed) {
                        if (isdigit(in[i - 1])) {
                            digit_allowed = false;
                            ws_allowed = true;
                        }
                        else {
                            // Ex: '510 123 4567'
                            return CSV_STRING;
                        }
                    }
                    break;
                case '-':
                    if (!neg_allowed) {
                        // Ex: '510-123-4567'
                        return CSV_STRING;
                    }

                    neg_allowed = false;
                    break;
                case '.':
                    if (!dot_allowed) {
                        return CSV_STRING;
                    }

                    dot_allowed = false;
                    prob_float = true;
                    break;
                case 'e':
                case 'E':
                    // Process scientific notation
                    if (prob_float) {
                        size_t exponent_start_idx = i + 1;

                        // Strip out plus sign
                        if (in[i + 1] == '+') {
                            exponent_start_idx++;
                        }

                        return _process_potential_exponential(
                            in.substr(exponent_start_idx),
                            neg_allowed ? integral_part + decimal_part : -(integral_part + decimal_part),
                            out
                        );
                    }

                    return CSV_STRING;
                    break;
                default:
                    if (current >= '0' && current <= '9') {
                        // Process digit
                        has_digit = true;

                        if (!digit_allowed)
                            return CSV_STRING;
                        else if (ws_allowed) // Ex: '510 456'
                            ws_allowed = false;

                        // Build current number
                        unsigned digit = current - '0';
                        if (prob_float) {
                            decimal_part += digit / pow10(++places_after_decimal);
                        }
                        else {
                            integral_part = (integral_part * 10) + digit;
                        }
                    }
                    else {
                        return CSV_STRING;
                    }
                }
            }

            // No non-numeric/non-whitespace characters found
            if (has_digit) {
                long double number = integral_part + decimal_part;
                if (out) {
                    *out = neg_allowed ? number : -number;
                }

                return prob_float ? CSV_DOUBLE : _determine_integral_type(number);
            }

            // Just whitespace
            return CSV_NULL;
        }
    }
}