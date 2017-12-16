#include <iostream>
#include <vector>
#include <list>
#include <string>
#include <unordered_map>

using std::vector;
using std::string;
using std::unordered_map;
using std::list;

namespace csv_parser {
    /** @file */

    namespace helpers {
        /** @name String Formatting Functions
          */
          ///@{
        string rpad_trim(string in, size_t n = 20, size_t trim = 80);
        vector<string> round(vector<long double> in);
        ///@}

        /** @name Pretty Printing Functions
          */
          ///@{
        vector<size_t> _get_col_widths(vector<vector<string>>&, size_t);
        void print_record(std::vector<std::string> record);
        void print_table(vector<vector<string>> &records,
            int row_num = 0, vector<string> row_names = {},
            bool header=false);
        ///@}

        template<typename T>
        inline vector<string> to_string(vector<T> record) {
            // Convert a vector of non-strings to a vector<string>
            vector<string> ret_vec = {};
            for (auto item : record) {
                ret_vec.push_back(std::to_string(item));
            }
            return ret_vec;
        }

        template<typename T>
        inline void _min_val_to_front(list<T>& seq) {
            /** Move the unordered_mapped element with the smallest value to the front */
            auto min_p = seq.begin();
            for (auto it = seq.begin(); it != seq.end(); ++it)
                if ((*it)->second < (*min_p)->second)
                    min_p = it;

            // Swap
            if (min_p != seq.begin()) {
                auto temp = *min_p;
                seq.erase(min_p);
                seq.push_front(temp);
            }
        }

        template<typename T1, typename T2>
        inline unordered_map<T1, T2> top_n_values(unordered_map<T1, T2> in, size_t n) {
            /** Return a unordered_map with only the top n values */
            list<typename unordered_map<T1, T2>::iterator> top_n; // Ptrs to top values

            // Initialize with first n values
            auto it = in.begin();
            for (; (it != in.end()) && (top_n.size() < n); ++it)
                top_n.push_back(it);

            _min_val_to_front(top_n);  // Keep smallest value at front

            // Loop through unordered_map
            for (; it != in.end(); ++it) {
                if (it->second > (top_n.front())->second) {
                    top_n.pop_front();      // Swap values
                    top_n.push_front(it);
                    _min_val_to_front(top_n);
                }
            }

            unordered_map<T1, T2> new_unordered_map;
            for (auto it = top_n.begin(); it != top_n.end(); ++it)
                new_unordered_map[(*it)->first] = in[(*it)->first];

            return new_unordered_map;
        }
    }
}