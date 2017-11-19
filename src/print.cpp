#include "print.h"
#include <algorithm>
#include <list>

using std::list;

namespace csv_parser {
    /** @file */

    string pad(string in, size_t n, size_t trim) {
        /** Add extra whitespace until string is n characters long */
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
        /** Take a numeric vector and return a string vector with rounded numbers */
        vector<string> new_vec;
        char buffer[100];
        string rounded;

        for (auto num = std::begin(in); num != std::end(in); ++num) {
            snprintf(buffer, 100, "%.2Lf", *num);
            rounded = std::string(buffer);
            new_vec.push_back(rounded);
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

    void print_table(vector<vector<string>> &records,
        vector<string> row_names) {

        /* Find out width of each column */
        vector<size_t> col_widths = _get_col_widths(records, 80);
        size_t row_name_width = 0;

        // Find out length of row names column
        if (row_names.size() > 0) {
            for (auto r_name = std::begin(row_names); r_name != std::end(row_names);
                ++r_name) {

                if ((*r_name).size() + 3 > row_name_width) {
                    row_name_width = (*r_name).size() + 3;
                }
            }
        }

        // Print out several vectors as a table
        size_t col_width_p = 0;
        size_t col_width_base = 0;
        auto row_name_p = row_names.begin();

        size_t temp_col_size = 0;
        size_t temp_row_width = 0; // Flag for when to break long rows
        vector<vector<string>::iterator> cursor = {}; // Save position in string vectors

        // Initialize cursors
        for (auto record_p = records.begin(); record_p != records.end(); ++record_p)
            cursor.push_back((*record_p).begin());

        for (size_t current_row = 0, rlen = records.size();
            current_row < rlen;
            current_row++) {

            // Print out row name (if applicable)
            if (row_name_p != row_names.end()) {
                std::cout << pad(*row_name_p, row_name_width);
                ++row_name_p;
            }

            // Print out one row --> Break if necessary
            while (temp_row_width < 80 && col_width_p != col_widths.size()) {
                temp_col_size = col_widths[col_width_p];
                std::cout << pad(*(cursor[current_row]), temp_col_size);

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
                std::cout << std::endl;
                row_name_p = row_names.begin();
                current_row = -1;

                col_width_base = 0;
                for (size_t i = cursor[0] - records[0].begin(); i > 0; i--) {
                    ++col_width_base;
                    col_width_p = col_width_base;
                }
            }
        }

        records.clear();
    }
}