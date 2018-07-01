#include "data_type.h"

namespace csv::internals {
    #ifndef DOXYGEN_SHOULD_SKIP_THIS
    std::string type_name(const DataType& dtype) {
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

    DataType data_type(std::string_view in, long double* const out) {
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

        bool ws_allowed = true;
        bool neg_allowed = true;
        bool dot_allowed = true;
        bool digit_allowed = true;
        bool has_digit = false;
        bool prob_float = false;

        unsigned places_after_decimal = 0;
        long double num_buff = 0;

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
                else {
                    neg_allowed = false;
                }
                break;
            case '.':
                if (!dot_allowed) {
                    return CSV_STRING;
                }
                else {
                    dot_allowed = false;
                    prob_float = true;
                }
                break;
            default:
                if (isdigit(current)) {
                    // Process digit
                    has_digit = true;

                    if (!digit_allowed)
                        return CSV_STRING;
                    else if (ws_allowed) // Ex: '510 456'
                        ws_allowed = false;

                    // Build current number
                    unsigned digit = current - '0';
                    if (num_buff == 0) {
                        num_buff = digit;
                    }
                    else if (prob_float) {
                        num_buff += (long double)digit / pow(10.0, ++places_after_decimal);
                    }
                    else {
                        num_buff *= 10;
                        num_buff += digit;
                    }
                }
                else {
                    return CSV_STRING;
                }
            }
        }

        // No non-numeric/non-whitespace characters found
        if (has_digit) {
            if (!neg_allowed) num_buff *= -1;
            if (out) *out = num_buff;

            if (prob_float)
                return CSV_DOUBLE;
            else {
                long double log10_num_buff;
                if (!neg_allowed) log10_num_buff = log10(-num_buff);
                else log10_num_buff = log10(num_buff);

                if (log10_num_buff < log10(std::numeric_limits<int>::max()))
                    return CSV_INT;
                else if (log10_num_buff < log10(std::numeric_limits<long int>::max()))
                    return CSV_LONG_INT;
                else if (log10_num_buff < log10(std::numeric_limits<long long int>::max()))
                    return CSV_LONG_LONG_INT;
                else // Conversion to long long will cause an overflow
                    return CSV_DOUBLE;
            }
        }
        else {
            // Just whitespace
            return CSV_NULL;
        }
    }
}