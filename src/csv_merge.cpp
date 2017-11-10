# include "csv_parser.h"
# include "util.h"
# include <regex>
# include <iostream>
# include <stdexcept>
# include <set>

using std::vector;
using std::string;

namespace csv_parser {
    vector<string> get_col_names(string filename, int row) {
        /* Get the column names of a CSV file */
        CSVReader reader(",", "\"", row);
        reader.read_csv(filename, row + 1);
        return reader.get_col_names();
    }
    
    void merge(string outfile, vector<string> in) {
        /* Merge several CSV files together */
        std::set<string> col_names = {};
        vector<string> first_col_names;
        vector<string> temp_col_names;
        
        for (string infile: in) {
            /* Make sure columns are the same across all files
             * Currently assumes header is on first line
             */
            
            if (col_names.empty()) {
                // Read first CSV
                first_col_names = get_col_names(infile, 0);
                
                for (string cname: first_col_names) {
                    col_names.insert(cname);
                }
            } else {
                temp_col_names = get_col_names(infile, 0);
                
                if (temp_col_names.size() < col_names.size()) {
                    throw std::runtime_error("Inconsistent columns.");
                }
                
                /*
                for (string cname: temp_col_names) {
                }
                */
            }
        }
        
        // Begin merging
        CSVCleaner writer;
        bool csv_append = false;
        
        for (string infile: in) {
            writer.read_csv(infile);
            
            if (!csv_append) {
                writer.to_csv(outfile);
                csv_append = true;
            } else {
                writer.to_csv(outfile, true, 0, true);
            }
        }
    }
    
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