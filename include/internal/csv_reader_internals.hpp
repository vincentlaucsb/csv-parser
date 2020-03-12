#pragma once
#include <array>
#include <deque>
#include <functional>
#include <fstream>
#include <memory>
#include <string>

#include "compatibility.hpp"
#include "csv_row.hpp"
#include "row_buffer.hpp"

namespace csv {
    namespace internals {
        /**  @typedef ParseFlags
         *   An enum used for describing the significance of each character
         *   with respect to CSV parsing
         */
        enum ParseFlags {
            NOT_SPECIAL, /**< Characters with no special meaning */
            QUOTE,       /**< Characters which may signify a quote escape */
            DELIMITER,   /**< Characters which may signify a new field */
            NEWLINE      /**< Characters which may signify a new row */
        };

        using ParseFlagMap = std::array<ParseFlags, 256>;
        using WhitespaceMap = std::array<bool, 256>;

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        HEDLEY_CONST CONSTEXPR ParseFlagMap make_parse_flags(char delimiter, char quote_char) {
            std::array<ParseFlags, 256> ret = {};
            for (int i = -128; i < 128; i++) {
                const int arr_idx = i + 128;
                char ch = char(i);

                if (ch == delimiter)
                    ret[arr_idx] = DELIMITER;
                else if (ch == quote_char)
                    ret[arr_idx] = QUOTE;
                else if (ch == '\r' || ch == '\n')
                    ret[arr_idx] = NEWLINE;
                else
                    ret[arr_idx] = NOT_SPECIAL;
            }

            return ret;
        }

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character c and, v[i + 128] is true if
         *  c is a whitespace character
         */
        HEDLEY_CONST CONSTEXPR WhitespaceMap make_ws_flags(const char * ws_chars, size_t n_chars) {
            std::array<bool, 256> ret = {};
            for (int i = -128; i < 128; i++) {
                const int arr_idx = i + 128;
                char ch = char(i);
                ret[arr_idx] = false;

                for (size_t j = 0; j < n_chars; j++) {
                    if (ws_chars[j] == ch) {
                        ret[arr_idx] = true;
                    }
                }
            }

            return ret;
        }

        struct ParseData {
            csv::string_view in;
            ParseFlagMap parse_flags;
            WhitespaceMap ws_flags;
            BufferPtr row_buffer;
            bool& quote_escape; /*< Whether or not we are currently in a quote escaped field */
            std::deque<CSVRow>& records;
        };

        CSV_INLINE BufferPtr parse(const ParseData& data);
        CSV_INLINE void write_record(const ParseData& data);

        /** Read the first 500KB of a CSV file */
        CSV_INLINE std::string get_csv_head(csv::string_view filename);
    }
}