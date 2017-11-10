#ifndef UTIL_H
#define UTIL_H

#include <iostream>
#include <vector>
#include <deque>
#include <string>

using std::vector;
using std::string;
using std::deque;

namespace csv_parser {
    inline string pad(string in, int n=20);
    inline vector<double> round(vector<double> in);

    inline void print_table(
        vector<vector<string>*> records,
        deque<string> row_names = {});

    template<typename T>
    inline void print_record(std::vector<T> &record) {
        // Print out a single CSV row
        for (T field : record) {
            std::string temp = std::to_string(field);
            std::cout << temp << " ";
        }

        std::cout << std::endl;
    }

    template<>
    inline void print_record(std::vector<std::string> &record) {
        // Print out a single CSV row
        for (std::string field : record) {
            std::cout << pad(field, 20) << " ";
        }

        std::cout << std::endl;
    }

    template<typename T>
    inline vector<string> to_string(vector<T> record) {
        // Convert a vector of non-strings to a vector<string>
        vector<string> ret_vec = {};
        for (auto item : record) {
            ret_vec.push_back(std::to_string(item));
        }
        return ret_vec;
    }

    inline void print_table(
        vector<vector<string>*> records,
        deque<string> row_names) {

        /* Find out width of each column
         */
        vector<size_t> col_widths = {};
        size_t row_name_width = 0;
        size_t col_width = 0;
        bool first_row = true;

        // Find out length of row names column
        if (row_names.size() > 0) {
            for (auto r_name = std::begin(row_names); r_name != std::end(row_names);
                ++r_name) {

                if ((*r_name).size() + 3 > row_name_width) {
                    row_name_width = (*r_name).size() + 3;
                }
            }
        }

        // Looping through rows
        for (auto row = std::begin(records); row != std::end(records); ++row) {
            // Looping through columns
            for (size_t i = 0; i < (**row).size(); i++) {
                // Get size of string (add 3 for padding)
                col_width = (**row)[i].size() + 3;

                if (first_row) {
                    // Set initial column widths
                    col_widths.push_back(col_width);
                }
                else if (col_width > col_widths[i]) {
                    // Update col_width if this field is a big boy
                    col_widths[i] = col_width;
                }
            }

            first_row = false;
        }

        // Print out several vectors as a table
        size_t col = 0;

        for (auto record_p = records.begin(); record_p != records.end(); ++record_p) {
            // Print out row name (if applicable)
            if (!row_names.empty()) {
                std::cout << pad(*row_names.begin(), row_name_width);
                row_names.pop_front();
            }

            // Print out this row
            for (string field : **record_p) {
                std::cout << pad(field, col_widths[col]);
                col++;
                if (col >= col_widths.size()) {
                    col = 0;
                }
            }

            std::cout << std::endl;
        }

        records.clear();
    }

    inline string pad(string in, int n) {
        // Add extra whitespace until string is n characters long
        std::string new_str = in;

        for (int i = in.size(); i + 1 < n; i++) {
            new_str += " ";
        }

        return new_str;
    }
    
    inline vector<string> round(vector<long double> in) {
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

    /*
    template<typename T1, typename T2>
    inline map<T1, T2> top_n_values(map<T1, T2> in, int n) {
        // Return a map with only the top n values
        auto it = in.begin();

        // Rolling vector of pointers to top n values
        vector<map<T1, T2>::iterator> top_n = { it };
        T2 min = *it;
        T2 max = *it;
        ++it;

        // Initialize
        while (top_n.size() < 10) {
            if (*it < min) {
                min = *it;
            }
            else if (*it > max) {
                max = *it;
            }

            top_n.push_back(it);
            ++it;
        }

        // Loop through map
        for (; it != in.end(); ++it) {
            if (*it < min) {
                // Replace

            }
            else if (*it > max) {
                // Replace

            }
        }
    }*/
}

#endif