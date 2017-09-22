/* Lightweight CSV Parser */

# include <iostream>
# include <vector>
# include <queue>
# include <stdexcept>
# include <fstream>
# include <math.h>

namespace csvmorph {
    class CSVReader {
        public:
            void read_csv(std::string filename, bool carriage_return);
            std::vector<std::string> get_col_names();
            void set_col_names(std::vector<std::string>);
            void feed(std::string &in);
            void end_feed();
            std::vector<std::string> pop();
            bool empty();
            void print_csv();
            void to_csv(std::string);
            void to_json(std::string);
            int row_num = 0;
            CSVReader(
                std::string delim=",",
                std::string quote="\"",
                int header=-1,
                std::vector<int> subset_= std::vector<int>{});
        protected:
            void process_possible_delim(std::string&, size_t&);
            void process_quote(std::string&, size_t&);
            void process_newline(std::string&, size_t&);
            void write_record(std::vector<std::string>&);
            std::vector<std::string> col_names;
            
            // Indices of columns to subset
            std::vector<int> subset;
            
            // Actual column names of subset columns
            std::vector<std::string> subset_col_names;
            
            char delimiter;
            char quote_char;
            bool quote_escape;
            int header_row;
            std::queue< std::vector < std::string > > records;
            std::vector<std::string> record_buffer;
            std::string str_buffer;
    };
    
    // CSVReader Member functions
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
        /* Parse RFC 4180 compliant CSV files */
        
        for (size_t i = 0, ilen = in.length(); i < ilen; i++) {
            if (in[i] == this->delimiter) {
                // Case 1: Possible delimiter
                this->process_possible_delim(in, i);
            } else if (in[i] == this->quote_char) {
                // Case 2: Possible quote escape
                this->process_quote(in, i);
            } else {
                switch(in[i]) {
                    case '\r':
                    case '\n':
                        // Case 3: Newline
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
            // Don't try to parse an empty buffer
            this->write_record(this->record_buffer);
        }
    }

    void CSVReader::process_possible_delim(std::string &in, size_t &index) {
        // Process a delimiter character and determine if it is a field separator
        
        if (this->quote_escape) {
            // Case: We are currently in a quote-escaped field
            // -> Write char as data
            this->str_buffer += in[index];
        } else {
            // Case: Not being escaped --> Write field
            this->record_buffer.push_back(this->str_buffer);
            this->str_buffer.clear();
        }
    }

    void CSVReader::process_newline(std::string &in, size_t &index) {
        // Process a newline character and determine if it is a record separator
        // May not get called if reader is fed via getline()
        
        if (this->quote_escape) {
            // Case: We are currently in a quote-escaped field
            // -> Write char as data
            this->str_buffer += in[index];
        } else {
            // Case: Carriage Return Line Feed, Carriage Return, or Line Feed
            // => End of record -> Write record
            if ((in[index] == '\r') && (in[index + 1] == '\n')) {
                index++;
            }
            
            // Write remaining data
            if (this->str_buffer.size() > 0) {
                this->record_buffer.push_back(this->str_buffer);
            }
            
            this->str_buffer.clear();
            
            // Write record
            this->write_record(this->record_buffer);
        }
    }

    void CSVReader::process_quote(std::string &in, size_t &index) {
        // Determine if the usage of a quote is valid or throw an error
        
        // Case: We are currently in a quote-escaped field
        if (this->quote_escape) {
            if ((in[index + 1] == this->delimiter) || 
                (in[index + 1] == '\r') ||
                (in[index + 1] == '\n')) {
                // Case: Next character is delimiter or newline
                // --> End of field
                this->quote_escape = false;
            } else if (in[index + 1] == this->quote_char) {
                // Case: Next character is quote --> This is a quote escape
                this->str_buffer += in[index] + in[index];
                index++;
            } else {
                // Not RFC 1480 compliant --> Double up the quotes so it is
                this->str_buffer += in[index] + in[index];
            }
        } else {
            // Case 1: Not case 1 + previous character was delimiter
            if (in[index - 1] == this->delimiter) {
                this->quote_escape = true;
            } else if (in[index + 1] == this->quote_char) {
                // Case 2: Two quotes follow each other
                // Possible empty field, e.g. "Value","","Value"
                // Treat as empty field if we have the sequence "",
                if ((in[index + 2] == this->delimiter)
                    || (in[index + 2] == '\r')
                    || (in[index + 2] == '\n')) {
                    this->record_buffer.push_back(std::string());
                    index += 2;
                } else {
                    throw std::runtime_error("Unexpected usage of quotes.");
                }
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
            // Temporary fix: CSV parser doesn't always catch
            // the last field if it is empty
            if (record.size() + 1 == this->col_names.size()) {
                record.push_back(std::string());
            }
            
            // Make sure record is of the right length
            if (record.size() == this->col_names.size()) {
                if (this->subset.size() > 0) {
                    // Subset the data
                    std::vector<std::string> subset_record;
                    
                    for (size_t i = 0; i < this->subset.size(); i++) {
                        int subset_index = this->subset[i];
                        subset_record.push_back(record[subset_index]);
                    }
                    
                    this->records.push(subset_record);
                } else {
                    this->records.push(record);
                }
            } else {
                // Case 1: Zero-length record. Probably caused by
                // extraneous delimiters.
                // Case 2: Too short or too long
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

    bool CSVReader::empty() {
        // Return true or false if CSV queue is empty
        return this->records.empty();
    }

    void CSVReader::read_csv(
        std::string filename,
        bool carriage_return=true) {
        std::ifstream infile(filename);
        std::string line;
        char delim;
        
        if (carriage_return) {
            // Works for files terminated by \r\n
            delim = '\r';
        } else {
            delim = '\n';
        }
        
        while (std::getline(infile, line, delim)) {
            this->feed(line);
            this->quote_escape = false;
        }
        
        this->end_feed();
        infile.close();
    }
    
    void CSVReader::to_csv(std::string filename) {
        // Write queue to CSV file
        std::string row;
        std::vector<std::string> record;
        std::ofstream outfile;
        outfile.open(filename);
        
        while (!this->records.empty()) {
            // Remove and return first CSV row
            std::vector< std::string > record = this->records.front();
            this->records.pop();
            
            for (size_t i = 0; i < record.size(); i++) {
                row += "\"" + record[i] + "\"";
                if (i + 1 != record.size()) {
                    row += ",";
                }
            }
            
            outfile << row << "\r\n";
            row.clear();
        }
        outfile.close();
    }
    
        
    void CSVReader::to_json(std::string filename) {
        // Write queue to CSV file
        std::string row;
        std::vector<std::string> record;
        std::string * col_name;
        std::string json_record;
        std::ofstream outfile;
        outfile.open(filename);
        
        outfile << "[";
        
        while (!this->records.empty()) {
            // Remove and return first CSV row
            record = this->records.front();
            this->records.pop();
            json_record = "{";
            
            // Create JSON record
            for (size_t i = 0; i < this->subset_col_names.size(); i++) {
                col_name = &this->subset_col_names[i];
                json_record += "\"" + *col_name + "\": ";
                json_record += "\"" + record[i] + "\"";
                if (i + 1 != record.size()) {
                    json_record += ",";
                }
            }
            
            if (!this->records.empty()) {
                json_record += "},\n";
            } else {
                json_record += "}";
            }
            
            outfile << json_record;
            json_record.clear();                
        }
        
        outfile << "]";
        outfile.close();
    }
    
    void CSVReader::print_csv() {
        while (!this->records.empty()) {
            std::vector< std::string > record = this->records.front();
            this->records.pop();
            
            for (int j = 0; j < record.size(); j++) {
                std::cout << record[j] << '\t';
            }
            
            std::cout << std::endl;
        }
    }    
}