# include "csv_parser.h"
# include "data_type.cpp"
# include <iostream>
# include <stdexcept>
# include <fstream>
# include <math.h>

namespace csv_parser {
    CSVReader::CSVReader(
        std::string delim,
        std::string quote,
        int header,
        std::vector<int> subset_) {
        // Type cast std::string to char
        delimiter = delim[0];
        quote_char = quote[0];
        
        quote_escape = false;
        header_row = header;
        subset = subset_;
    }

    void CSVReader::set_col_names(std::vector<std::string> col_names) {
        this->col_names = col_names;
        
        if (this->subset.size() > 0) {
            this->subset_flag = true;
            for (size_t i = 0; i < this->subset.size(); i++) {
                subset_col_names.push_back(col_names[this->subset[i]]);
            }
        } else {
            // "Subset" is every column
            for (size_t i = 0; i < this->col_names.size(); i++) {
                this->subset.push_back(i);
            }
            subset_col_names = col_names;
        }
    }

    std::vector<std::string> CSVReader::get_col_names() {
        return this->col_names;
    }

    void CSVReader::feed(std::string &in) {
        // Parse RFC 4180 compliant CSV files
        for (size_t i = 0, ilen = in.length(); i < ilen; i++) {
            if (in[i] == this->delimiter) {
                this->process_possible_delim(in, i);
            } else if (in[i] == this->quote_char) {
                this->process_quote(in, i);
            } else {
                switch(in[i]) {
                    case '\r':
                    case '\n':
                        this->process_newline(in, i);
                        break;
                    default:
                        this->str_buffer += in[i];
                }
            }
        }
    }

    void CSVReader::end_feed() {
        // Indicate that there is no more data to receive, and parse remaining
        // content in string buffer
        if (this->record_buffer.size() > 0) {
            this->write_record(this->record_buffer);
        }
    }

    void CSVReader::process_possible_delim(std::string &in, size_t &index) {
        // Process a delimiter character and determine if it is a field separator
        if (!this->quote_escape) {
            // Case: Not being escaped --> Write field
            this->record_buffer.push_back(this->str_buffer);
            this->str_buffer.clear();
        } else {
            this->str_buffer += in[index]; // Treat as regular data
        }
    }

    void CSVReader::process_newline(std::string &in, size_t &index) {
        // Process a newline character and determine if it is a record separator        
        if (!this->quote_escape) {
            // Case: Carriage Return Line Feed, Carriage Return, or Line Feed
            // => End of record -> Write record
            if ((in[index] == '\r') && (in[index + 1] == '\n')) {
                index++;
            }
            
            // Write remaining data
            if (this->str_buffer.size() > 0) {
                this->record_buffer.push_back(this->str_buffer);
                this->str_buffer.clear();
            }
            
            // Write record
            this->write_record(this->record_buffer);
        } else {
            this->str_buffer += in[index]; // Quote-escaped
        }
    }

    void CSVReader::process_quote(std::string &in, size_t &index) {
        // Determine if the usage of a quote is valid or fix it
        if (this->quote_escape) {
            if ((in[index + 1] == this->delimiter) || 
                (in[index + 1] == '\r') ||
                (in[index + 1] == '\n')) {
                // Case: End of field
                this->quote_escape = false;
            } else {
                // Note: This may fix single quotes (not valid) by doubling them up
                this->str_buffer += in[index];
                this->str_buffer += in[index];
                
                if (in[index + 1] == this->quote_char) {
                    index++;  // Case: Two consecutive quotes
                }
            }
        } else {
            if (in[index - 1] == this->delimiter) {
                // Case 1: Previous character was delimiter
                this->quote_escape = true;
            } else {
                // Case 2: Unescaped quote => Drop it
            }
        }
    }

    void CSVReader::write_record(std::vector<std::string> &record) {
        // Unset all flags
        this->quote_escape = false;
        
        /*
        std::ofstream outfile;
        outfile.open("debugging.txt", std::ios::app);
        outfile << "Record Size: " << record.size() << std::endl;
        outfile << "Columns: " << this->col_names.size() << std::endl;
        */
        
        if (this->row_num > this->header_row) {
            /* Workaround: CSV parser doesn't catch the last field if
             * it is empty */
            if (record.size() + 1 == this->col_names.size()) {
                record.push_back(std::string());
            }
            
            // Make sure record is of the right length
            if (record.size() == this->col_names.size()) {
                if (!this->subset_flag) {
                    // No need to subset
                    this->records.push(record);
                } else {
                    // Subset the data
                    std::vector<std::string> subset_record;
                    
                    for (size_t i = 0; i < this->subset.size(); i++) {
                        subset_record.push_back(record[ this->subset[i] ]);
                    }
                    
                    this->records.push(subset_record);
                }
            } else {
                // Case 1: Zero-length record. Probably caused by
                // extraneous delimiters.
                // Case 2: Too short or too long
                // std::cout << "Dropping row" << std::endl;
            }
        } else if (this->row_num == this->header_row) {
            this->set_col_names(record);
        } else {
            // Ignore rows before header row     
        }
        
        this->row_num++;
        record.clear();
    }

    std::vector<std::string> CSVReader::pop() {
        // Remove and return first CSV row
        std::vector< std::string > record = this->records.front();
        this->records.pop();
        return record;
    }
    
    std::map<std::string, std::string> CSVReader::pop_map() {
        // Remove and return first CSV row as an std::map
        std::vector< std::string > record = this->pop();
        std::map< std::string, std::string > record_map;
        
        for (size_t i = 0; i < subset.size(); i ++) {
            record_map[ this->subset_col_names[i] ] = record[i];
        }
        
        return record_map;
    }

    bool CSVReader::empty() {
        return this->records.empty();
    }

    void CSVReader::read_csv(std::string filename) {
        std::ifstream infile(filename);
        std::string line;
        std::string newline("\n");
        
        while (std::getline(infile, line, '\n')) {
            /* Hack: Add the delimiter back in because it might be
             * in a quoted field and thus not an actual delimiter
             */
            line += newline;
            this->feed(line);
        }

        // Done reading
        this->end_feed();
        infile.close();
    }
    
    std::string CSVReader::csv_to_json() {
        // Helper method for to_json()
        std::vector<std::string> record = this->pop();
        std::string json_record = "{";
        std::string * col_name;
        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            col_name = &this->subset_col_names[i];
            json_record += "\"" + *col_name + "\":";
            
            /* Quote strings but not numeric fields
             * Recall data_type() returns 2 for ints and 3 for floats
             */
            
            if (data_type(record[i]) > 1) {
                json_record += record[i];
            } else {
                json_record += "\"" + json_escape(record[i]) + "\"";
            }
            
            if (i + 1 != record.size()) {
                json_record += ",";
            }
        }
        
        json_record += "}";
        return json_record;
    }
    
    void CSVReader::to_json(std::string filename) {
        /* Write CSV as a newline-delimited JSON file
         * TODO: Handle integers and floats correctly (instead of leaving
         * them as strings)
         */
        std::vector<std::string> record;
        std::string json_record;
        std::ofstream outfile;
        outfile.open(filename);
        
        while (!this->empty()) {
            json_record = this->csv_to_json();
            if (!this->empty()) { json_record += "\n"; }
            outfile << json_record;
        }
        
        outfile.close();
    }
    
    std::vector<std::string> CSVReader::to_json() {
        // Same as above but writes to memory
        std::vector<std::string> record;
        std::string json_record;
        std::vector<std::string> output;
        
        while (!this->empty()) {
            json_record = this->csv_to_json();
            output.push_back(json_record);
        }
        
        return output;
    }
    
    std::string json_escape(std::string in) {
        // Given a CSV string, convert it to a JSON string with proper
        // escaping as described by RFC 7159
        
        std::string out;
        
        for (size_t i = 0, ilen = in.length(); i < ilen; i++) {
            switch (in[i]) {
                case '"':
                    // Assuming quotes come in pairs due to CSV escaping
                    out += "\\\"";
                    i++; // Skip over next quote
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '/':
                    out += "\\/";
                    break;
                case '\r':
                    out += "\\\r";
                    break;
                case '\n':
                    out += "\\\n";
                    break;
                case '\t':
                    out += "\\\t";
                    break;
                default:
                    out += in[i];
            }
        }
        
        return out;
    }
}