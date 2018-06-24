#pragma once
/** @csv */
#include <iostream>
#include <vector>
#include <string>
#include <fstream>

namespace csv {
    template<char Delim = ',', char Quote = '"'>
    inline std::string csv_escape(const std::string& in, const bool quote_minimal = true) {
        /** Format a string to be RFC 4180-compliant
        *  @param[in]  in              String to be CSV-formatted
        *  @param[out] quote_minimal   Only quote fields if necessary.
        *                              If False, everything is quoted.
        */

        std::string new_string;
        bool quote_escape = false;     // Do we need a quote escape
        new_string += Quote;           // Start initial quote escape sequence

        for (size_t i = 0; i < in.size(); i++) {
            switch (in[i]) {
            case Quote:
                new_string += std::string(2, Quote);
                quote_escape = true;
                break;
            case Delim:
                quote_escape = true;
                // Do not break;
            default:
                new_string += in[i];
            }
        }

        if (quote_escape || !quote_minimal) {
            new_string += Quote; // Finish off quote escape
            return new_string;
        }
        else {
            return in;
        }
    }

    /** Class for writing CSV files.
    *
    *  See csv::csv_escape() for a function that formats a non-CSV string.
    *
    *  To write to a CSV file, one should
    *   -# Initialize a DelimWriter with respect to some file
    *   -# Call write_row() on std::vector<std::string>s of unformatted text
    */
    template<class OutputStream, char Delim, char Quote>
    class DelimWriter {
    public:
        DelimWriter(OutputStream& _out) : out(_out) {};
        DelimWriter(const std::string& filename) : DelimWriter(std::ifstream(filename)) {};

        void write_row(const std::vector<std::string>& record, bool quote_minimal = true) {
            /** Format a sequence of strings and write to CSV according to RFC 4180
            *
            *  **Note**: This does not check to make sure row lengths are consistent
            *  @param[in]  record          Vector of strings to be formatted
            *  @param      quote_minimal   Only quote fields if necessary
            */

            for (size_t i = 0, ilen = record.size(); i < ilen; i++) {
                out << csv_escape<Delim, Quote>(record[i]);
                if (i + 1 != ilen) out << Delim;
            }

            out << std::endl;
        }

        DelimWriter& operator<<(std::vector<std::string>& record) {
            this->write_row(record);
            return *this;
        }

    private:
        OutputStream & out;
    };

    template<typename OutputStream, char Delim, char Quote>
    inline DelimWriter<OutputStream, Delim, Quote> make_writer(OutputStream& out) {
        return DelimWriter<OutputStream, Delim, Quote>(out);
    }

    template<typename OutputStream>
    inline DelimWriter<OutputStream, ',', '"'> make_csv_writer(OutputStream& out) {
        return DelimWriter<OutputStream, ',', '"'>(out);
    }

    template<typename OutputStream>
    inline DelimWriter<OutputStream, '\t', '"'> make_tsv_writer(OutputStream& out) {
        return DelimWriter<OutputStream, '\t', '"'>(out);
    }
}