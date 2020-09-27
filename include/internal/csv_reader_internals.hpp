#pragma once
#include <deque>
#include <functional>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "compatibility.hpp"
#include "csv_format.hpp"
#include "raw_csv_data.hpp"

namespace csv {
    namespace internals {
        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        HEDLEY_CONST CONSTEXPR ParseFlagMap make_parse_flags(char delimiter) {
            std::array<ParseFlags, 256> ret = {};
            for (int i = -128; i < 128; i++) {
                const int arr_idx = i + 128;
                char ch = char(i);

                if (ch == delimiter)
                    ret[arr_idx] = ParseFlags::DELIMITER;
                else if (ch == '\r' || ch == '\n')
                    ret[arr_idx] = ParseFlags::NEWLINE;
                else
                    ret[arr_idx] = ParseFlags::NOT_SPECIAL;
            }

            return ret;
        }

        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */
        HEDLEY_CONST CONSTEXPR ParseFlagMap make_parse_flags(char delimiter, char quote_char) {
            std::array<ParseFlags, 256> ret = make_parse_flags(delimiter);
            ret[(size_t)quote_char + 128] = ParseFlags::QUOTE;
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

        struct GuessScore {
            double score;
            size_t header;
        };

        CSV_INLINE GuessScore calculate_score(csv::string_view head, CSVFormat format);

        CSVGuessResult _guess_format(csv::string_view head, const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' });

        CSV_INLINE std::string get_csv_head(
            csv::string_view filename
        );

        /** Read the first 500KB of a CSV file */
        CSV_INLINE std::string get_csv_head(
            csv::string_view filename,
            size_t file_size
        );
    }
}