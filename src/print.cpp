#include "print.h"
#include <algorithm>
#include <list>

using std::list;

namespace csv_parser {
    /** @file */

    namespace helpers {
        string rpad_trim(string in, size_t n, size_t trim) {
            /**
            * Add extra whitespace until string is n characters long
            * Also trim string if it is too long
            */
            std::string new_str = in;

            if (in.size() <= trim) {
                for (size_t i = in.size(); i + 1 < n; i++)
                    new_str += " ";
            }
            else {
                new_str = in.substr(0, trim);
            }

            return new_str;
        }

        vector<string> round(vector<long double> in) {
            /**
             * Take a numeric vector and return a string vector with rounded numbers
             * Also replace NaNs with empty strings
             */
            vector<string> new_vec;
            char buffer[100];
            string rounded;

            for (auto num = std::begin(in); num != std::end(in); ++num) {
                if (isnan(*num)) {
                    new_vec.push_back("");
                }
                else {
                    snprintf(buffer, 100, "%.2Lf", *num);
                    rounded = std::string(buffer);
                    new_vec.push_back(rounded);
                }
            }

            return new_vec;
        }

        vector<size_t> _get_col_widths(
            vector<vector<string>> &records,
            size_t max_col_width) {
            /** Given a list of string vectors representing rows to print,
            *  compute the width of each column
            *
            *  Rules
            *   - Doesn't return column widths > max_col_width
            */

            vector<size_t> col_widths = {};
            bool first_row = true;
            size_t col_width;

            for (auto row = std::begin(records); row != std::end(records); ++row) {
                // Looping through columns
                for (size_t i = 0; i < (*row).size(); i++) {
                    // Get size of string (plus 3 for padding)
                    col_width = (*row)[i].size() + 3;
                    if (col_width > max_col_width)
                        col_width = max_col_width;

                    // Set initial column widths
                    if (first_row)
                        col_widths.push_back(col_width);

                    // Update col_width if this field is a big boy
                    else if (col_width > col_widths[i])
                        col_widths[i] = col_width;
                }

                first_row = false;
            }

            return col_widths;
        }

        void print_record(std::vector<std::string> record) {
            // Print out a single CSV row
            for (std::string field : record) {
                std::cout << rpad_trim(field, 20) << " ";
            }

            std::cout << std::endl;
        }

        void print_table(
            vector<vector<string>> &records,
            int row_num,
            vector<string> row_names,
            bool header
        ) {
            /*
            * Set row_num to -1 to disable row number printing
            * Or set row_names to disable number printing
            */

            /* Find out width of each column */
            vector<size_t> col_widths = _get_col_widths(records, 100);
            const int orig_row_num = row_num;

            // Set size of row names column
            size_t row_name_width = 10;
            for (auto it = row_names.begin(); it != row_names.end(); ++it)
                if ((*it).size() > row_name_width)
                    row_name_width = (*it).size();

            // Print out several vectors as a table
            auto row_name_p = row_names.begin();
            size_t col_width_p = 0, col_width_base = 0, temp_col_size = 0;
            size_t temp_row_width = 0; // Flag for when to break long rows

            // Store position in string vectors
            vector<vector<string>::iterator> cursor = {};
            for (auto record_p = records.begin(); record_p != records.end(); ++record_p)
                cursor.push_back((*record_p).begin());

            for (size_t current_row = 0, rlen = records.size();
                current_row < rlen; current_row++) {

                // Hide row number for first row if header=true
                if (!row_names.empty()) {
                    std::cout << rpad_trim(*(row_name_p++), row_name_width);
                }
                else if (row_num >= 0) {
                    if (header && (row_num == orig_row_num))
                        std::cout << rpad_trim(" ", row_name_width);
                    else
                        std::cout << rpad_trim("[" + std::to_string(row_num) + "]", row_name_width);
                    row_num++;
                }

                // Print out one row --> Break if necessary
                while (temp_row_width < 80 && col_width_p != col_widths.size()) {
                    temp_col_size = col_widths[col_width_p];

                    /*
                    if (header && (row_num == orig_row_num + 1))
                        std::cout << rpad_trim("----", temp_col_size) << std::endl;
                    else
                    */
                    std::cout << rpad_trim(*(cursor[current_row]), temp_col_size);

                    temp_row_width += temp_col_size;
                    ++col_width_p;         // Advance col width iterator
                    ++cursor[current_row]; // Advance row iterator
                }

                // Prepare to move to next row
                col_width_p = col_width_base;
                temp_row_width = 0;
                std::cout << std::endl;

                // Check if we need to restart the loop
                if ((current_row + 1 == rlen) && cursor[0] != records[0].end()) {
                    if (!row_names.empty())
                        row_name_p = row_names.begin();

                    std::cout << std::endl;
                    row_num = orig_row_num;
                    col_width_base = col_width_p = cursor[0] - records[0].begin();
                    current_row = -1;
                }
            }
            records.clear();
        }
    }
}