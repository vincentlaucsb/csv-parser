/** @file
 *  @brief Implements Functions related to hexadecimal parsing
 */

#pragma once
#include <type_traits>
#include <cmath>

#include "common.hpp"

namespace csv {
    namespace internals {
        template<typename T>
        bool try_parse_hex(csv::string_view sv, T& parsedValue) {
            static_assert(std::is_integral<T>::value, 
                "try_parse_hex only works with integral types (int, long, long long, etc.)");
            
            size_t start = 0, end = 0;

            // Trim out whitespace chars
            for (; start < sv.size() && sv[start] == ' '; start++);
            for (end = start; end < sv.size() && sv[end] != ' '; end++);
            
            T value_ = 0;

            size_t digits = (end - start);
            size_t base16_exponent = digits - 1;

            if (digits == 0) return false;

            for (const auto& ch : sv.substr(start, digits)) {
                int digit = 0;

                switch (ch) {
                    case '0':
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9':
                        digit = static_cast<int>(ch - '0');
                        break;
                    case 'a':
                    case 'A':
                        digit = 10;
                        break;
                    case 'b':
                    case 'B':
                        digit = 11;
                        break;
                    case 'c':
                    case 'C':
                        digit = 12;
                        break;
                    case 'd':
                    case 'D':
                        digit = 13;
                        break;
                    case 'e':
                    case 'E':
                        digit = 14;
                        break;
                    case 'f':
                    case 'F':
                        digit = 15;
                        break;
                    default:
                        return false;
                }

                value_ += digit * (T)pow(16, (double)base16_exponent);
                base16_exponent--;
            }

            parsedValue = value_;
            return true;
        }
    }
}