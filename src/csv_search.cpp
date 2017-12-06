#include "csv_parser.h"
#include "print.h"
#include <regex>

using std::vector;
using std::string;

namespace csv_parser {
    /** @file */

    void head(std::string infile, int nrow,
        std::string delim, std::string quote, int header,
        std::vector<int> subset) {
        /** Print out the first n rows of a CSV */
        if (delim == "")
            delim = guess_delim(infile);

        CSVReader reader(delim, quote, header, subset);
        vector<vector<string>> records = {};
        int i = 0;

        for (auto it = reader.begin(infile); it != reader.end(); ++it) {
            if (records.empty())
                records.push_back(reader.get_col_names());
            records.push_back(*it);
            i++;

            if (i%nrow == 0) {
                print_table(records, i - nrow);
                std::cout << std::endl
                    << "Press Enter to continue printing, or q or Ctrl + C to quit."
                    << std::endl << std::endl;
                if (std::cin.get() == 'q') {
                    reader.close();
                    break;
                }
            }
        }
    }

    void grep(std::string infile, int col, std::string match, int max_rows,
        std::string delim, std::string quote, int header,
        std::vector<int> subset) {
        std::regex reg_pattern(match);
        std::smatch matches;
        const int orig_max_rows = max_rows;

        if (delim == "")
            delim = guess_delim(infile);

        CSVReader reader(delim);
        vector<vector<string>> records = {};
        
        for (auto it = reader.begin(infile); it != reader.end() && max_rows != 0; ++it) {
            if (records.empty())
                records.push_back(reader.get_col_names());

            std::regex_search((*it)[col], matches, reg_pattern);
            if (!matches.empty()) {
                records.push_back(*it);
                max_rows--;
            }

            if (max_rows == 0) {
                print_table(records);
                std::cout << std::endl
                    << "Press Enter to continue searching, or q or Ctrl + C to quit."
                    << std::endl << std::endl;
                
                if (std::cin.get() == 'q') {
                    reader.close();
                    break;
                }
                max_rows = orig_max_rows;
            }
        }
    }
}