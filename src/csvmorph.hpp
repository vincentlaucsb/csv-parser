# include <vector>
# include <queue>
# include <map>

namespace csvmorph {
    // Supports streaming parsing of CSV files
    class CSVReader {
        public:
            void read_csv(std::string filename);
            void feed(std::string &in);
            void end_feed();
            std::vector<std::string> pop();
            bool empty();
            void print_csv();
            void to_csv(std::string);
            void to_json(std::string);
            CSVReader(
                std::string delim=",",
                std::string quote="\"",
                std::vector<std::string> col_names_ = std::vector<std::string> {},
                std::vector<int> subset_= std::vector<int>{});
        protected:
            void process_possible_delim(std::string&, size_t&);
            void process_quote(std::string&, size_t&);
            void process_newline(std::string&, size_t&);
            void write_record(std::vector<std::string>&);
            std::vector<std::string> col_names;
            std::vector<std::string> subset_col_names;
            char delimiter;
            char quote_char;
            bool quote_escape;
            std::vector<int> subset;
            std::queue< std::vector < std::string > > records;
            std::vector<std::string> record_buffer;
            std::string str_buffer;
    };
    
    // Calculates statistics from CSV files
    class CSVStat: public CSVReader {
        public:
            std::vector<long double> get_mean();
            std::vector<long double> get_variance();
            std::vector<long double> get_mins();
            std::vector<long double> get_maxes();
            std::vector< std::map<std::string, int> > get_counts();
            std::vector< std::map<int, int> > get_dtypes();
            void calc(bool, bool, bool);
            CSVStat(
                std::string,
                std::string,
                std::vector<std::string> col_names_ = std::vector<std::string>{},
                std::vector<int> subset_ = std::vector<int>{});
        private:
            // An array of rolling averages
            // Each index corresponds to the rolling mean for the column at said index
            std::vector<long double> rolling_means;
            std::vector<long double> rolling_vars;
            std::vector<long double> mins;
            std::vector<long double> maxes;
            std::vector<int> n;
            
            // Statistic calculators
            void variance(long double&, size_t&);
            void count(std::string&, size_t&);
            void dtype(std::string&, size_t&);
            void min_max(long double&, size_t&);
            
            // Map column indices to counters
            std::map<int, std::map<std::string, int>> counts;
            std::map<int, std::map<int, int>> dtypes;
    };
}