# include "csv_parser.h"

using std::vector;
using std::string;

namespace csv_parser {
    /** @file */

    void reformat(std::string infile, std::string outfile, int skiplines) {
        /** Reformat a CSV file */
        CSVReader reader(guess_delim(infile));
        CSVWriter writer(outfile);
        reader.read_csv(infile);
        writer.write_row(reader.get_col_names()); // Write Column Names

        // Write Records
        while (!reader.empty()) {
            while (skiplines > 0) {
                reader.pop();
                skiplines--;
            }
                
            writer.write_row(reader.pop());
        }

        writer.close();
    }

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
        CSVWriter writer(outfile);
        
        for (string infile: in) {
            delim = guess_delim(infile);
            CSVReader reader(delim);
            reader.read_csv(infile);
            
            while (!reader.empty())
                writer.write_row(reader.pop());
        }

        writer.close();
    }
}