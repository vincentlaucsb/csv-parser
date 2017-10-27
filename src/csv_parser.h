/* Lightweight CSV Parser */

# include <string>
# include <vector>
# include <queue>
# include <map>

namespace csv_parser {    
    int data_type(std::string&);

    class CSVReader {
        public:
            void read_csv(std::string filename, bool carriage_return=true);
            std::vector<std::string> get_col_names();
            void set_col_names(std::vector<std::string>);
            void feed(std::string &in);
            void end_feed();
            std::vector<std::string> pop();
            bool empty();
            void print_csv();
            void to_csv(std::string, bool);
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
}