#include "csv_parser.h"

namespace csv_parser {
    std::string csv_escape(std::string& in, bool quote_minimal) {
        /** Format a string to be RFC 4180-compliant
         *  @param      quote_minimal   Only quote fields if necessary
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

    void CSVWriter::to_csv(std::string filename, bool quote_minimal,
        int skiplines, bool append) {
        /** Output currently parsed rows (including column names)
         *  to a RFC 4180-compliant CSV file.
         *  @param[out] filename        File to save to
         *  @param      quote_minimal   Only quote fields if necessary
         *  @param      skiplines       Number of lines (after header) to skip
         *  @param      append          Append to an existing CSV file
         */

        // Write queue to CSV file
        std::string row;
        std::vector<std::string> record;
        std::ofstream outfile;

        if (append) {
            outfile.open(filename, std::ios_base::binary | std::ios_base::app);
        }
        else {
            outfile.open(filename, std::ios_base::binary);

            // Write column names
            for (size_t i = 0, ilen = this->col_names.size(); i < ilen; i++) {
                outfile << this->col_names[i];
                if (i + 1 != ilen)
                    outfile << ",";
            }
            outfile << "\r\n";
        }

        // Skip lines
        while (!this->records.empty() && skiplines > 0) {
            this->records.pop_front();
            skiplines--;
        }

        // Write records
        while (!this->records.empty()) {
            // Remove and return first CSV row
            std::vector< std::string > record = this->pop();

            for (size_t i = 0, ilen = record.size(); i < ilen; i++) {
                // Calculate data type statistics
                this->dtype(record[i], i);

                if ((quote_minimal &&
                    (record[i].find_first_of(',')
                        != std::string::npos))
                    || !quote_minimal) {
                    row += "\"" + record[i] + "\"";
                }
                else {
                    row += record[i];
                }

                if (i + 1 != ilen) { row += ","; }
            }

            outfile << row << "\r\n";
            row.clear();
        }

        outfile.close();
    }

    /*
    void CSVWriter::to_postgres(std::string filename, bool quote_minimal,
        int skiplines, bool append) {
        /** Generate a PostgreSQL dump file
         *  @param[out] filename        File to save to
         *  @param      skiplines       Number of lines (after header) to skip
         */
        
        // Write queue to CSV file
    /*
        std::string row;
        std::vector<std::string> record;
        std::ofstream outfile;

        while (!this->eof) {
            this->read_csv(filename, 100000, false);
            // this->to_csv(filename, )
        }
    }
*/
}