/* Lightweight CSV Parser */

// g++ -std=c++11 -O3 -o test -g _parser2.cpp
// Debugging: g++ -std=c++11 -o test -g _parser.cpp
// ./test

# include "csvmorph.hpp"
# include "data_type.hpp"
# include <iostream>
# include <vector>
# include <queue>
# include <map>
# include <stdexcept>
# include <fstream>
# include <math.h>

namespace csvmorph {
    // CSVReader Member functions
    CSVReader::CSVReader(
        std::string delim,
        std::string quote,
        std::vector<std::string> col_names_,
        std::vector<int> subset_) {
        // Type cast std::string to char
        delimiter = delim[0];
        quote_char = quote[0];
        
        quote_escape = false;
        col_names = col_names_;
        subset = subset_;
        
        if (subset.size() > 0) {
            for (size_t i = 0; i < subset.size(); i++) {
                subset_col_names.push_back(col_names_[subset_[i]]);
            }
        } else {
            subset_col_names = col_names_;
        }
    }

    void CSVReader::feed(std::string &in) {
        /* Parse RFC 4180 compliant CSV files */
        /*
        for (int i = 0; i < in.length(); i++) {
            if (in[i] == this->delimiter) {
                // Case 1: Possible delimiter
                this->process_possible_delim(in, i);
            } else if (in[i] == this->quote_char) {
                // Case 2: Possible quote escape
                this->process_quote(in, i);
            } else if ((in[i] == '\r') || (in[i] == '\n')) {
                // Case 3: Newline
                this->process_newline(in, i);
            } else {
                // Case 4: Data --> Write it
                this->str_buffer += in[i];
            }
        }
        */
        
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

    void CSVReader::read_csv(std::string filename) {
        std::ifstream infile;
        std::string line;
        infile.open(filename);
        
        if (infile.is_open()) {
            while (std::getline(infile, line)) {
                this->feed(line);
            }
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
    
    // CSVStat Member Functions
    CSVStat::CSVStat(
        std::string delim,
        std::string quote,
        std::vector<std::string> col_names_,
        std::vector<int> subset_) {
        // Type cast from std::string to char
        delimiter = delim[0];
        quote_char = quote[0];
        
        quote_escape = false;
        col_names = col_names_;
        subset = subset_;
        
        if (subset.size() > 0) {
            for (size_t i = 0; i < subset.size(); i++) {
                subset_col_names.push_back(col_names_[subset_[i]]);
            }
        } else {
            subset_col_names = col_names_;
        }
        
        // Initialize arrays to 0
        for (size_t i = 0; i < subset_.size(); i++) {
            rolling_means.push_back(0);
            rolling_vars.push_back(0);
            mins.push_back(NAN);
            maxes.push_back(NAN);
            n.push_back(0);
        }
    }
    
    std::vector<long double> CSVStat::get_mean() {
        // Return current means
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset.size(); i++) {
            ret.push_back(this->rolling_means[i]);
        }
        return ret;
    }
    
    std::vector<long double> CSVStat::get_variance() {
        // Return current variances
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset.size(); i++) {
            ret.push_back(this->rolling_vars[i]/(this->n[i] - 1));
        }
        return ret;
    }
    
    std::vector<long double> CSVStat::get_mins() {
        // Return current variances
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset.size(); i++) {
            ret.push_back(this->mins[i]);
        }
        return ret;
    }
    
    std::vector<long double> CSVStat::get_maxes() {
        // Return current variances
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset.size(); i++) {
            ret.push_back(this->maxes[i]);
        }
        return ret;
    }
    
    std::vector< std::map<std::string, int> > CSVStat::get_counts() {
        // Get counts for each column
        std::vector< std::map<std::string, int> > ret;        
        for (size_t i = 0; i < this->subset.size(); i++) {
            ret.push_back(this->counts[i]);
        }
        return ret;
    }
    
    std::vector< std::map<int, int> > CSVStat::get_dtypes() {
        // Get data type counts for each column
        std::vector< std::map<int, int> > ret;        
        for (size_t i = 0; i < this->subset.size(); i++) {
            ret.push_back(this->dtypes[i]);
        }
        return ret;
    }
    
    void CSVStat::calc(
        bool numeric=true,
        bool count=true,
        bool dtype=true) {
        /* Go through all records and calculate specified statistics
         * numeric: Calculate all numeric related statistics
         */
        std::vector<std::string> current_record;
        long double x_n;

        while (!this->records.empty()) {
            current_record = this->records.front();
            this->records.pop();
            
            for (size_t i = 0; i < this->subset.size(); i++) {
                if (count) {
                    this->count(current_record[i], i);
                } if (dtype) {
                    this->dtype(current_record[i], i);
                }
                
                // Numeric Stuff
                if (numeric) {
                    try {
                        x_n = std::stold(current_record[i]);
                        
                        // This actually calculates mean AND variance
                        this->variance(x_n, i);
                        this->min_max(x_n, i);
                    } catch(std::invalid_argument) {
                        // Ignore for now 
                        // In the future, save number of non-numeric arguments
                    } catch(std::out_of_range) {
                        // Ignore for now                         
                    }
                }
            }
        }
    }
    
    void CSVStat::dtype(std::string &record, size_t &i) {
        // Given a record update the type counter
        int type = data_type(record);
        
        if (this->dtypes[i].find(type) !=
            this->dtypes[i].end()) {
            // Increment count
            this->dtypes[i][type]++;
        } else {
            // Initialize count
            this->dtypes[i].insert(std::make_pair(type, 1));
        }
    }
    
    void CSVStat::count(std::string &record, size_t &i) {
        // Given a record update the according count
        if (this->counts[i].find(record) !=
            this->counts[i].end()) {
            // Increment count
            this->counts[i][record]++;
        } else {
            // Initialize count
            this->counts[i].insert(std::make_pair(record, 1));
        }
    }
    
    void CSVStat::min_max(long double &x_n, size_t &i) {
        // Update current minimum and maximum
        if (std::isnan(this->mins[i])) {
            this->mins[i] = x_n;
        } if (std::isnan(this->maxes[i])) {
            this->maxes[i] = x_n;
        }
        
        if (x_n < this->mins[i]) {
            this->mins[i] = x_n;
        } else if (x_n > this->maxes[i]) {
            this->maxes[i] = x_n;
        } else {
        }
    }
    
    void CSVStat::variance(long double &x_n, size_t &i) {
        // Given a record update rolling mean and variance for all columns
        // using Welford's Algorithm
        long double * current_rolling_mean = &this->rolling_means[i];
        long double * current_rolling_var = &this->rolling_vars[i];
        int * current_n = &this->n[i];
        long double delta;
        long double delta2;
        
        if (*current_n == 0) {
            // If current n obvs = 0
            *current_rolling_mean = x_n;
        } else {
            delta = x_n - *current_rolling_mean;
            *current_rolling_mean += delta/(*current_n);
            delta2 = x_n - *current_rolling_mean;
            *current_rolling_var += delta*delta2;
        }
        
        *current_n = *current_n + 1;
    }
}