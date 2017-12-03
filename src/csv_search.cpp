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
        if (delim == "") {
            delim = guess_delim(infile);
        }

        CSVReader reader(delim, quote, header, subset);
        reader.read_csv(infile, nrow, false);

        vector<vector<string>> records = { reader.get_col_names() };
        vector<string> row_names = {};
        int i = 0;

        while (!(reader.eof) || !(reader.empty())) {
            for (int j = 0; j < nrow; j++) {
                row_names.push_back("[" + std::to_string(i) + "]");
                i++;
            }

            while (!reader.empty())
                records.push_back(reader.pop());

            print_table(records, row_names);
            records.clear();
            row_names.clear();

            std::cout << std::endl;
            std::cout << "Press Enter to continue printing, or q or Ctrl + C to quit." << std::endl;
            std::cout << std::endl;

            if (std::cin.get() != 'q') {
                reader.read_csv(infile, nrow, false);
            }
            else {
                reader.infile.close();
                break;
            }
        }
    }

    void grep(std::string infile, int col, std::string match, int max_rows,
        std::string delim, std::string quote, int header,
        std::vector<int> subset) {
        std::regex reg_pattern(match);
        std::smatch matches;
        const int orig_max_rows = max_rows;

        if (delim == "") {
            delim = guess_delim(infile);
        }

        CSVReader reader(delim);
        reader.read_csv(infile);

        vector<vector<string>> records = { reader.get_col_names() };
        auto it = std::begin(reader.records);

        while (it != std::end(reader.records)) {
            for (; (it != std::end(reader.records) && (max_rows != 0));
                ++it) {
                std::regex_search((*it)[col], matches, reg_pattern);
                if (!matches.empty()) {
                    records.push_back(*it);
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