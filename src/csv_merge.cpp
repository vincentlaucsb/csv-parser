# include "csv_parser.h"
# include <regex>
# include <iostream>
# include <stdexcept>
# include <set>

namespace csv_parser {
    std::vector<std::string> get_col_names(std::string filename, int row) {
        /* Get the column names of a CSV file */
        CSVReader reader(",", "\"", row);
        reader.read_csv(filename, row + 1);
        return reader.get_col_names();
    }
    
    void print_record(std::vector<std::string> &record) {
        // Print out a single CSV row
        for (std::string field: record) {
            std::cout << field << " ";
        }
        
        std::cout << std::endl;
    }
    
    void merge(std::string outfile, std::vector<std::string> in) {
        /* Merge several CSV files together */
        std::set<std::string> col_names = {};
        std::vector<std::string> first_col_names;
        std::vector<std::string> temp_col_names;
        
        for (std::string infile: in) {
            /* Make sure columns are the same across all files
             * Currently assumes header is on first line
             */
            
            if (col_names.empty()) {
                // Read first CSV
                first_col_names = get_col_names(infile, 0);
                
                for (std::string cname: first_col_names) {
                    col_names.insert(cname);
                }
            } else {
                temp_col_names = get_col_names(infile, 0);
                
                if (temp_col_names.size() < col_names.size()) {
                    throw std::runtime_error("Inconsistent columns.");
                }
                
                /*
                for (std::string cname: temp_col_names) {
                }
                */
            }
        }
        
        // Begin merging
        CSVCleaner writer;
        bool csv_append = false;
        
        for (std::string infile: in) {
            writer.read_csv(infile);
            
            if (!csv_append) {
                writer.to_csv(outfile);
                csv_append = true;
            } else {
                writer.to_csv(outfile, true, 0, true);
            }
        }
    }
    
    void head(std::string infile, int nrow) {
        CSVReader reader;
        reader.read_csv(infile, nrow);
        
        std::vector<std::string> record;
        std::vector<std::string> col_names = reader.get_col_names();
        
        print_record(col_names);
        
        while (!reader.empty()) {
            record = reader.pop();
            print_record(record);
        }
    }
    
    void grep(std::string infile, int col, std::string match) {
        std::smatch matches;
        std::regex reg_pattern(match);
        
        CSVReader reader;
        reader.read_csv(infile);
        
        while (!reader.empty()) {
            std::vector<std::string> record = reader.pop();
            std::regex_search(record[col], matches, reg_pattern);
            
            if (!matches.empty()) {
                print_record(record);
            }
        }
    }
}