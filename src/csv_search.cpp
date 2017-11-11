#include "csv_parser.h"
#include "print.h"
#include <regex>

using std::vector;
using std::string;

namespace csv_parser {
    void head(string infile, int nrow,
        string delim, string quote, int header, vector<int> subset) {
        /** Print out the first n rows of a CSV */
        if (delim == "") {
            delim = guess_delim(infile);
        }

        CSVReader reader(delim, quote, header, subset);
        reader.read_csv(infile, nrow);

        vector<string> col_names = reader.get_col_names();
        vector<vector<string>*> records = { &col_names };

        for (auto it = std::begin(reader.records);
            it != std::end(reader.records); ++it) {
            records.push_back(&(*it));
        }

        print_table(records);
    }

    void grep(string infile, int col, string match, int max_rows,
        string delim, string quote, int header, vector<int> subset) {
        std::regex reg_pattern(match);
        std::smatch matches;
        const int orig_max_rows = max_rows;

        if (delim == "") {
            delim = guess_delim(infile);
        }

        CSVReader reader(delim);
        reader.read_csv(infile);

        vector<string> col_names = reader.get_col_names();
        vector<vector<string>*> records = { &col_names };
        auto it = std::begin(reader.records);

        while (it != std::end(reader.records)) {
            for (; (it != std::end(reader.records) && (max_rows != 0));
                ++it) {
                std::regex_search((*it)[col], matches, reg_pattern);
                if (!matches.empty()) {
                    records.push_back(&(*it));
                    max_rows--;
                }
            }

            print_table(records);
            records.clear();

            // Paging
            if (it != std::end(reader.records)) {
                max_rows = orig_max_rows;
                std::cout << "Press enter to keep printing, q to quit" << std::endl;

                if (std::cin.get() == 'q')
                    break;
                else
                    continue;
            }
        }
    }
}