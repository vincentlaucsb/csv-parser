#include "csv_reader_internals.hpp"

#include <iostream>

namespace csv {
    namespace internals {
        CSV_INLINE GuessScore calculate_score(csv::string_view head, CSVFormat format) {
            // Frequency counter of row length
            std::unordered_map<size_t, size_t> row_tally = { { 0, 0 } };

            // Map row lengths to row num where they first occurred
            std::unordered_map<size_t, size_t> row_when = { { 0, 0 } };

            // Parse the CSV
            BasicCSVParser parser(
                internals::make_parse_flags(format.get_delim(), '"'),
                internals::make_ws_flags({}, 0)
            );

            ThreadSafeDeque<CSVRow> rows;
            parser.parse(head, rows);

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

            // Final score is equal to the largest 
            // row size times rows of that size
            for (auto& [row_size, row_count] : row_tally) {
                double score = (double)(row_size * row_count);
                if (score > final_score) {
                    final_score = score;
                    header_row = row_when[row_size];
                }
            }

            return {
                final_score,
                header_row
            };
        }

        /** Guess the delimiter used by a delimiter-separated values file */
        CSV_INLINE CSVGuessResult _guess_format(csv::string_view head, const std::vector<char>& delims) {
            /** For each delimiter, find out which row length was most common.
             *  The delimiter with the longest mode row length wins.
             *  Then, the line number of the header row is the first row with
             *  the mode row length.
             */

            CSVFormat format;
            size_t max_score = 0,
                   header = 0;
            char current_delim = delims[0];

            for (char cand_delim : delims) {
                auto result = calculate_score(head, format.delimiter(cand_delim));

                if (result.score > max_score) {
                    max_score = (size_t)result.score;
                    current_delim = cand_delim;
                    header = result.header;
                }
            }

            return { current_delim, (int)header };
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename) {
            const size_t bytes = 500000;
            std::ifstream infile(filename.data());
            if (!infile.is_open()) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            std::unique_ptr<char[]> buffer(new char[bytes + 1]);
            char * head_buffer = buffer.get();

            for (size_t i = 0; i < bytes + 1; i++) {
                head_buffer[i] = '\0';
            }

            infile.read(head_buffer, bytes);
            return std::string(head_buffer);
        }
    }
}