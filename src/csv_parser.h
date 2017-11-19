/** @csv_parser */
/* Lightweight CSV Parser */

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <math.h>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace csv_parser {    
    /** @file */

    struct CSVFileInfo {
        std::string filename;
        std::vector<std::string> col_names;
        std::string delim;
        int n_rows;
        int n_cols;
    };

    /** @name Helpers
      */
    ///@{
    int data_type(std::string&);
    std::string json_escape(std::string);
    ///@}
    
    /** @name Search Functions
      */
    ///@{
    void head(std::string infile, int nrow = 100,
        std::string delim = "", std::string quote = "\"",
        int header = 0, std::vector<int> subset = {});
    void grep(std::string infile, int col, std::string match, int max_rows = 500,
        std::string delim = "", std::string quote = "\"",
        int header = 0, std::vector<int> subset = {});
    ///@}

    /** @name Utility functions
      */
    ///@{
    std::string guess_delim(std::string filename);
    std::vector<std::string> get_col_names(std::string filename,
        std::string delim = ",", std::string quote = "\"", int header = 0);
    int col_pos(std::string filename, std::string col_name,
        std::string delim = ",", std::string quote = "\"", int header = 0);
    CSVFileInfo get_file_info(std::string filename);
    ///@}

    /** @name CSV Functions
      */
    ///@{
    void merge(std::string outfile, std::vector<std::string> in);    
    ///@}

    /** The main class for parsing CSV files */
    class CSVReader {        
        public:
            /** @name CSV Input
             *  Functions for reading CSV files
             */
            ///@{
            void read_csv(std::string filename, int nrows=-1, bool close=true);
            std::vector<std::string> get_col_names();
            void set_col_names(std::vector<std::string>);
            void feed(std::string &in);
            void end_feed();
            ///@}
            
            /** @name Output
             *  Functions for working with parsed CSV rows
             */
            ///@{
            std::vector<std::string> pop();
            std::vector<std::string> pop_back();
            std::map<std::string, std::string> pop_map();
            bool empty();
            void to_json(std::string filename, bool append = false);
            std::vector<std::string> to_json();
            void sample(int n);
            ///@}

            std::deque< std::vector < std::string > > records; /**< Queue of parsed CSV rows */
            int row_num = 0; /**< How many lines have been parsed so far */
            bool eof = false;      /**< Have we reached the end of file */

            friend void head(std::string infile, int nrow,
                std::string delim, std::string quote,
                int header, std::vector<int> subset);

            friend void grep(std::string infile, int col, std::string match, int max_rows,
                std::string delim, std::string quote,
                int header, std::vector<int> subset);
            
            CSVReader(
                std::string delim=",",
                std::string quote="\"",
                int header=0,
                std::vector<int> subset_= std::vector<int>{});
        protected:
            // CSV parsing callbacks
            inline void process_possible_delim(std::string&, size_t&);
            inline void process_quote(std::string&, size_t&);
            inline void process_newline(std::string&, size_t&);
            inline void write_record(std::vector<std::string>&);
            
            // Helper methods
            inline std::string csv_to_json();
            
            // Column Information
            std::vector<std::string> col_names; /**< Column names */
            std::vector<int> subset; /**< Indices of columns to subset */
            std::vector<std::string> subset_col_names;
            bool subset_flag = false; /**< Set to true if we need to subset data */
                      
            // CSV settings and flags
            char delimiter;        /**< Delimiter character */
            char quote_char;       /**< Quote character */
            bool quote_escape;     /**< Parsing flag */
            int header_row;        /**< Line number of the header row (zero-indexed) */
            std::streampos last_pos = 0; /**< Line number of last row read from file */

            // Multi-threading support
            void _read_csv();      /**< Worker thread */
            std::deque<std::string*> feed_buffer;
            std::mutex feed_lock;
            std::condition_variable feed_cond;
            
            // Buffers
            std::ifstream infile;
            std::vector<std::string> record_buffer;            /**< Buffer for current row */
            std::string str_buffer;                            /**< Buffer for current string fragment */
    };
    
    /** Class for calculating statistics from CSV files */
    class CSVStat: public CSVReader {
        public:
            void calc(bool numeric=true, bool count=true, bool dtype=true);
            std::vector<long double> get_mean();
            std::vector<long double> get_variance();
            std::vector<long double> get_mins();
            std::vector<long double> get_maxes();
            std::vector< std::map<std::string, int> > get_counts();
            std::vector< std::map<int, int> > get_dtypes();
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
            void calc_col(size_t);
            
            // Map column indices to counters
            std::map<int, std::map<std::string, int>> counts;
    };

    /** Class for writing CSV files */
    class CSVCleaner: public CSVStat {
        public:
            void to_csv(std::string filename, bool quote_minimal=true, 
                int skiplines=0, bool append=false);
            using CSVStat::CSVStat;
    };
}