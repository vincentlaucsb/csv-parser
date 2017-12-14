# include "csv_parser.h"

using std::vector;
using std::string;

namespace csv_parser {
    /** @file */

    void reformat(std::string infile, std::string outfile, int skiplines) {
        /** Reformat a CSV file */
        CSVReader reader(infile);
        CSVWriter writer(outfile);
        vector<string> row;
        writer.write_row(reader.get_col_names());

        while(reader.read_row(row)) {
            for (; skiplines > 0; skiplines--)
                reader.read_row(row);
            writer.write_row(row);
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
        vector<string> first_col_names;
        vector<string> temp_col_names;
        
        for (string infile: in) {
            /* Make sure columns are the same across all files
             * Currently assumes header is on first line
             */
            
            if (col_names.empty()) {
                // Read first CSV
                first_col_names = get_col_names(infile);
                
                for (string cname: first_col_names) {
                    col_names.insert(cname);
                }
            } else {
                temp_col_names = get_col_names(infile);
                
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
        vector<string> row;
        
        for (string infile: in) {
            CSVReader reader(infile);          
            while (reader.read_row(row))
                writer.write_row(row);
        }

        writer.close();
    }
}