/* Cleans a CSV file while simulatenously producing data type statistics */

# include "csv_parser.hpp"
# include "data_type.hpp"
# include <iostream>
# include <vector>
# include <queue>
# include <map>
# include <stdexcept>
# include <fstream>
# include <math.h>

namespace csvmorph {
    class CSVCleaner: public CSVReader {
        public:
            std::vector< std::map<int, int> > get_dtypes();
            void to_csv(std::string, bool);
            CSVCleaner(
                std::string,
                std::string,
                int header=-1,
                std::vector<int> subset_ = std::vector<int>{});
        private:
            // Statistic calculators
            void dtype(std::string&, size_t&);
        
            // Map column indices to counters
            std::map<int, std::map<int, int>> dtypes;
    };
    
    // CSVStat Member Functions    
    CSVCleaner::CSVCleaner(
        std::string delim,
        std::string quote,
        int header,
        std::vector<int> subset_) {
        // Type cast from std::string to char
        delimiter = delim[0];
        quote_char = quote[0];
        
        quote_escape = false;
        header_row = header;
        subset = subset_;
    }
    
    void CSVCleaner::dtype(std::string &record, size_t &i) {
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
    
    void CSVCleaner::to_csv(std::string filename, bool quote_minimal=true) {
        // Write queue to CSV file
        std::string row;
        std::vector<std::string> record;
        std::ofstream outfile;
        outfile.open(filename);
        
        while (!this->records.empty()) {
            // Remove and return first CSV row
            std::vector< std::string > record = this->records.front();
            this->records.pop();            
            for (size_t i = 0, ilen = record.size(); i < ilen; i++) {
                // Calculate data type statistics
                this->dtype(record[i], i);
                
                if ((quote_minimal &&
                    (record[i].find_first_of(this->delimiter)
                        != std::string::npos))
                    || !quote_minimal) {
                    row += "\"" + record[i] + "\"";
                } else {
                    row += record[i];
                }
                
                if (i + 1 != ilen) { row += ","; }
            }
            
            outfile << row << "\n";
            row.clear();
        }
        outfile.close();
    }
    
    std::vector< std::map<int, int> > CSVCleaner::get_dtypes() {
        // Get data type counts for each column
        std::vector< std::map<int, int> > ret;        
        for (size_t i = 0; i < this->subset.size(); i++) {
            ret.push_back(this->dtypes[i]);
        }
        return ret;
    }
}