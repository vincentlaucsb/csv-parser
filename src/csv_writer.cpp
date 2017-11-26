#include "csv_parser.h"

using std::vector;
using std::string;

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

    CSVWriter::CSVWriter(std::string outfile) {
        // Open a file handler
        this->outfile = std::ofstream(outfile, std::ios_base::binary);
    }

    void CSVWriter::write_row(vector<string> record, bool quote_minimal) {
        /** Output currently parsed rows (including column names)
         *  to a RFC 4180-compliant CSV file.
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
        this->outfile.close();
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