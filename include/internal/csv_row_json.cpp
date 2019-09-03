#include "csv_row.hpp"

namespace csv {
    /*
    extra_space() and escape_string() from JSON for Modern C++
    
        __ _____ _____ _____
     __|  |   __|     |   | |  JSON for Modern C++
    |  |  |__   |  |  | | | |  version 3.7.0
    |_____|_____|_____|_|___|  https://github.com/nlohmann/json

    Licensed under the MIT License <http://opensource.org/licenses/MIT>.
    SPDX-License-Identifier: MIT
    Copyright (c) 2013-2019 Niels Lohmann <http://nlohmann.me>.

    Permission is hereby  granted, free of charge, to any  person obtaining a copy
    of this software and associated  documentation files (the "Software"), to deal
    in the Software  without restriction, including without  limitation the rights
    to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
    copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
    IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
    FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
    AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
    LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
    */

    namespace internals {
        /*!
         @brief calculates the extra space to escape a JSON string

         @param[in] s  the string to escape
         @return the number of characters required to escape string @a s

         @complexity Linear in the length of string @a s.
        */
        static std::size_t extra_space(const std::string& s) noexcept
        {
            std::size_t result = 0;


            for (const auto& c : s)
            {
                switch (c)
                {
                case '"':
                case '\\':
                case '\b':
                case '\f':
                case '\n':
                case '\r':
                case '\t':
                {
                    // from c (1 byte) to \x (2 bytes)
                    result += 1;
                    break;
                }


                default:
                {
                    if (c >= 0x00 && c <= 0x1f)
                    {
                        // from c (1 byte) to \uxxxx (6 bytes)
                        result += 5;
                    }
                    break;
                }
                }
            }


            return result;
        }

        static std::string escape_string(const std::string& s) noexcept
        {
            const auto space = extra_space(s);
            if (space == 0)
            {
                return s;
            }

            // create a result string of necessary size
            std::string result(s.size() + space, '\\');
            std::size_t pos = 0;

            for (const auto& c : s)
            {
                switch (c)
                {
                    // quotation mark (0x22)
                case '"':
                {
                    result[pos + 1] = '"';
                    pos += 2;
                    break;
                }


                // reverse solidus (0x5c)
                case '\\':
                {
                    // nothing to change
                    pos += 2;
                    break;
                }


                // backspace (0x08)
                case '\b':
                {
                    result[pos + 1] = 'b';
                    pos += 2;
                    break;
                }


                // formfeed (0x0c)
                case '\f':
                {
                    result[pos + 1] = 'f';
                    pos += 2;
                    break;
                }


                // newline (0x0a)
                case '\n':
                {
                    result[pos + 1] = 'n';
                    pos += 2;
                    break;
                }


                // carriage return (0x0d)
                case '\r':
                {
                    result[pos + 1] = 'r';
                    pos += 2;
                    break;
                }


                // horizontal tab (0x09)
                case '\t':
                {
                    result[pos + 1] = 't';
                    pos += 2;
                    break;
                }


                default:
                {
                    if (c >= 0x00 && c <= 0x1f)
                    {
                        // print character c as \uxxxx
                        sprintf(&result[pos + 1], "u%04x", int(c));
                        pos += 6;
                        // overwrite trailing null character
                        result[pos] = '\\';
                    }
                    else
                    {
                        // all other characters are added as-is
                        result[pos++] = c;
                    }
                    break;
                }
                }
            }

            return result;
        }
    }

    /**
     @brief Convert a CSV row to a JSON object
     */
    std::string CSVRow::to_json(const std::vector<std::string>& subset) const {
        std::string ret = "{";
        auto col_names = this->buffer->col_names->get_col_names();

        // TODO: Make subset do something
        for (size_t i = 0; i < this->n_cols; i++) {
            auto& col_name = col_names[i];

            // TODO: Possible performance enhancements by caching escaped column names
            auto field = this->operator[](i);
            ret += '"' + internals::escape_string(col_name) + "\":";

            if (field.is_num()) {
                // TODO: Make escape_string() use string_view
                 ret += internals::escape_string(field.get<>());
            }
            else {
                // Add quotes around strings
                ret += '"' + internals::escape_string(field.get<>()) + '"';
            }

            if (i + 1 < this->n_cols) {
                ret += ',';
            }
        }

        ret += '}';
        return ret;
    }
}