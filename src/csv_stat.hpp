/* Calculates statistics from CSV files */

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
    class CSVStat: public CSVReader {
        public:
            std::vector<long double> get_mean();
            std::vector<long double> get_variance();
            std::vector<long double> get_mins();
            std::vector<long double> get_maxes();
            std::vector< std::map<std::string, int> > get_counts();
            std::vector< std::map<int, int> > get_dtypes();
            void calc(bool, bool, bool);
            using CSVReader::CSVReader;
        protected:
            // Statistic calculators
            void dtype(std::string&, size_t&);
            
            // Map column indices to counters
            std::map<int, std::map<int, int>> dtypes;
        private:
            // An array of rolling averages
            // Each index corresponds to the rolling mean for the column at said index
            std::vector<long double> rolling_means;
            std::vector<long double> rolling_vars;
            std::vector<long double> mins;
            std::vector<long double> maxes;
            std::vector<float> n;
            void init_vectors();
            
            // Statistic calculators
            void variance(long double&, size_t&);
            void count(std::string&, size_t&);
            void min_max(long double&, size_t&);
            
            // Map column indices to counters
            std::map<int, std::map<std::string, int>> counts;
    };
    
    class CSVCleaner: public CSVStat {
        public:
            void to_csv(std::string, bool, int);
            using CSVStat::CSVStat;
    };
    
    void CSVStat::init_vectors() {
        // Initialize statistics arrays to NAN
        for (size_t i = 0; i < this->subset.size(); i++) {
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
        this->init_vectors();
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
        if (isnan(this->mins[i])) {
            this->mins[i] = x_n;
        } if (isnan(this->maxes[i])) {
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
        float * current_n = &this->n[i];
        long double delta;
        long double delta2;
        
        *current_n = *current_n + 1;
        
        if (*current_n == 1) {
            *current_rolling_mean = x_n;
        } else {
            delta = x_n - *current_rolling_mean;
            *current_rolling_mean += delta/(*current_n);
            delta2 = x_n - *current_rolling_mean;
            *current_rolling_var += delta*delta2;
        }
    }
    
    // CSVCleaner Member Functions       
    void CSVCleaner::to_csv(
        std::string filename,
        bool quote_minimal=true,
        int skiplines=0) {
        // Write queue to CSV file
        std::string row;
        std::vector<std::string> record;
        std::ofstream outfile;
        outfile.open(filename);
        
        // Write column names
        for (size_t i = 0; i < this->col_names.size(); i++) {
            outfile << this->col_names[i] + ",";
        }
        outfile << "\n";
        
        // Skip lines
        while (!this->records.empty() && skiplines > 0) {
            this->records.pop();
            skiplines--;
        }
        
        // Write records
        while (!this->records.empty()) {
            // Remove and return first CSV row
            std::vector< std::string > record = this->records.front();
            this->records.pop();            
            for (size_t i = 0, ilen = record.size(); i < ilen; i++) {
                // Calculate data type statistics
                this->dtype(record[i], i);
                
                if ((quote_minimal &&
                    (record[i].find_first_of(',')
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
}