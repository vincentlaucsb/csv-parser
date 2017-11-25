# include "csv_parser.h"

using std::vector;
using std::string;

namespace csv_parser {
    /** @file */

    void merge(
        std::string outfile,
        std::vector<std::string> in) {
        /** Merge several CSV files together
         *  @param delims A list of delimiters
         */
        std::set<string> col_names = {};
        std::string delim;
        vector<string> first_col_names;
        vector<string> temp_col_names;
        
        for (string infile: in) {
            /* Make sure columns are the same across all files
             * Currently assumes header is on first line
             */
            
            if (col_names.empty()) {
                // Read first CSV
                delim = guess_delim(infile);
                first_col_names = get_col_names(infile, delim, "\"", 0);
                
                for (string cname: first_col_names) {
                    col_names.insert(cname);
                }
            } else {
                delim = guess_delim(infile);
                temp_col_names = get_col_names(infile, delim, "\"", 0);
                
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
        CSVWriter writer;
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
}