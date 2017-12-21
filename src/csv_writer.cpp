#include "csv_parser.h"
#include <set>

using std::vector;
using std::string;

namespace csv {
    /** @file */
    std::string csv_escape(std::string& in, bool quote_minimal) {
        /** Format a string to be RFC 4180-compliant
         *  @param[in]  in              String to be CSV-formatted
         *  @param[out] quote_minimal   Only quote fields if necessary.
         *                              If False, everything is quoted.
         */

        std::string new_string = "\""; // Start initial quote escape sequence
        bool quote_escape = false;     // Do we need a quote escape

        for (size_t i = 0; i < in.size(); i++) {
            switch (in[i]) {
            case '"':
                new_string += "\"\"";
                quote_escape = true;
                break;
            case ',':
                quote_escape = true;
                // Do not break;
            default:
                new_string += in[i];
            }
        }

        if (quote_escape || !quote_minimal) {
            new_string += "\""; // Finish off quote escape
            return new_string;
        }
        else {
            return in;
        }
    }

    CSVWriter::CSVWriter(std::string outfile) {
        /** Open a file for writing
         *  @param[out] outfile Path of the file to be written to
         */
        this->outfile = std::ofstream(outfile, std::ios_base::binary);
    }

    void CSVWriter::write_row(vector<string> record, bool quote_minimal) {
        /** Format a sequence of strings and write to CSV according to RFC 4180
         *
         *  **Note**: This does not check to make sure row lengths are consistent
         *  @param[in]  record          Vector of strings to be formatted
         *  @param      quote_minimal   Only quote fields if necessary
         */

        for (size_t i = 0, ilen = record.size(); i < ilen; i++) {
            this->outfile << csv_escape(record[i]);
            if (i + 1 != ilen) 
                this->outfile << ",";
        }

        this->outfile << "\r\n";
    }

    void CSVWriter::close() {
        /** Close the file being written to */
        this->outfile.close();
    }

    void reformat(std::string infile, std::string outfile, int skiplines) {
        /** Reformat a CSV file
        *  @param[in]  infile    Path to existing CSV file
        *  @param[out] outfile   Path to file to write to
        *  @param[out] skiplines Number of lines past header to skip
        */

        CSVReader reader(infile);
        CSVWriter writer(outfile);
        vector<string> row;
        writer.write_row(reader.get_col_names());

        while (reader.read_row(row)) {
            for (; skiplines > 0; skiplines--)
                reader.read_row(row);
            writer.write_row(row);
        }

        writer.close();
    }

    void merge(std::string outfile, std::vector<std::string> in) {
        /** Merge several CSV files together
        *  @param[out] outfile Path to file to write to
        *  @param[in]  in      Vector of paths to input files
        */
        std::set<string> col_names = {};
        vector<string> first_col_names;
        vector<string> temp_col_names;

        for (string infile : in) {
            /* Make sure columns are the same across all files
            * Currently assumes header is on first line
            */

            if (col_names.empty()) {
                // Read first CSV
                first_col_names = get_col_names(infile);

                for (string cname : first_col_names) {
                    col_names.insert(cname);
                }
            }
            else {
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

        for (string infile : in) {
            CSVReader reader(infile);
            while (reader.read_row(row))
                writer.write_row(row);
        }

        writer.close();
    }
}