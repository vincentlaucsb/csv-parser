#include "print.h"

namespace csv_parser {
    string pad(string in, int n) {
        /** Add extra whitespace until string is n characters long */
        std::string new_str = in;

        for (size_t i = in.size(); i + 1 < n; i++) {
            new_str += " ";
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