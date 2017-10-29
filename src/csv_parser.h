/* Lightweight CSV Parser */

# include <string>
# include <vector>
# include <queue>
# include <map>

namespace csv_parser {    
    int data_type(std::string&);
    std::string json_escape(std::string);

    /** The main class for parsing CSV files */
    class CSVReader {        
        public:
            /** @name CSV Input
             *  Functions for reading CSV files
             */
            ///@{
            void read_csv(std::string filename);
            std::vector<std::string> get_col_names();
            void set_col_names(std::vector<std::string>);
            void feed(std::string &in);
            void end_feed();
            ///@}
            
            /** @name Output
             *  Functions for working with parsed CSV rows
             */
            ///@{
            inline std::vector<std::string> pop();
            inline std::map<std::string, std::string> pop_map();
            inline bool empty();
            void to_json(std::string);
            std::vector<std::string> to_json();
            ///@}
            
            int row_num = 0; /**< How many lines have been parsed so far */
            
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
            char delimiter;    /**< Delimiter character */
            char quote_char;   /**< Quote character */
            bool quote_escape; /**< Parsing flag */
            int header_row;    /**< Line number of the header row (zero-indexed) */
            
            // Buffers
            std::queue< std::vector < std::string > > records; /**< Queue of parsed CSV rows */
            std::vector<std::string> record_buffer;            /**< Buffer for current row */
            std::string str_buffer;                            /**< Buffer for current string fragment */
    };
    
    /** Class for calculating statistics from CSV files */
    class CSVStat: public CSVReader {
        public:
            void calc(bool, bool, bool);
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
            
            // Map column indices to counters
            std::map<int, std::map<std::string, int>> counts;
    };

    /** Class for writing CSV files */
    class CSVCleaner: public CSVStat {
        public:
            void to_csv(std::string, bool, int);
            using CSVStat::CSVStat;
    };
}