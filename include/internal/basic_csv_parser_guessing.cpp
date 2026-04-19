#include "basic_csv_parser.hpp"

#include <sstream>
#include <unordered_map>

namespace csv {
    namespace internals {
        CSV_INLINE GuessScore calculate_score(csv::string_view head, const CSVFormat& format) {
            // Frequency counter of row length
            std::unordered_map<size_t, size_t> row_tally = { { 0, 0 } };

            // Map row lengths to row num where they first occurred
            std::unordered_map<size_t, size_t> row_when = { { 0, 0 } };

            // Parse the CSV
            std::stringstream source(head.data());
            RowCollection rows;

            StreamParser<std::stringstream> parser(source, format, nullptr, false);
            parser.set_output(rows);
            parser.next();

            for (size_t i = 0; i < rows.size(); i++) {
                auto& row = rows[i];

                // Ignore zero-length rows
                if (row.size() > 0) {
                    if (row_tally.find(row.size()) != row_tally.end()) {
                        row_tally[row.size()]++;
                    }
                    else {
                        row_tally[row.size()] = 1;
                        row_when[row.size()] = i;
                    }
                }
            }

            double final_score = 0;
            size_t header_row = 0;
            size_t mode_row_length = 0;

            // Final score is equal to the largest row size times rows of that size.
            for (auto& pair : row_tally) {
                const size_t row_size = pair.first;
                const size_t row_count = pair.second;
                const double score = (double)(row_size * row_count);
                if (score > final_score) {
                    final_score = score;
                    mode_row_length = row_size;
                    header_row = row_when[row_size];
                }
            }

            // Heuristic: If first row has >= columns than mode, use it as header.
            size_t first_row_length = rows.size() > 0 ? rows[0].size() : 0;
            if (first_row_length >= mode_row_length && first_row_length > 0) {
                header_row = 0;
            }

            return { header_row, mode_row_length, final_score };
        }

        CSV_INLINE CSVGuessResult guess_format(csv::string_view head, const std::vector<char>& delims) {
            /** For each delimiter, find out which row length was most common (mode).
             *  The delimiter with the highest score (row_length * count) wins.
             *
             *  Header detection: If first row has >= columns than mode, use row 0.
             *  Otherwise use the first row with the mode length.
             */
            CSVFormat format;
            size_t max_score = 0;
            size_t header = 0;
            size_t n_cols = 0;
            char current_delim = delims[0];

            for (char cand_delim : delims) {
                auto result = calculate_score(head, format.delimiter(cand_delim));

                if ((size_t)result.score > max_score) {
                    max_score = (size_t)result.score;
                    current_delim = cand_delim;
                    header = result.header;
                    n_cols = result.mode_row_length;
                }
            }

            return { current_delim, (int)header, n_cols };
        }
    }
}
