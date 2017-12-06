/** @csv_parser */

#include <stdexcept>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <functional>
#include <algorithm>
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

    const size_t ITERATION_CHUNK_SIZE = 100000;

    struct CSVFormat {
        std::string delim;
        std::string quote_char;
    };

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
    std::vector<std::string> path_split(std::string);
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
    void reformat(std::string infile, std::string outfile, int skiplines=0);
    void merge(std::string outfile, std::vector<std::string> in);
    int csv_to_sql(std::string csv_file, std::string db, std::string table="");
    void csv_join(std::string filename1, std::string filename2, std::string outfile,
        std::string column1="", std::string column2="");
    std::string csv_escape(std::string& in, bool quote_minimal=true);
    ///@}

    /** @name SQL Functions
      */
    ///@{
    std::string sql_sanitize(std::string);
    std::vector<std::string> sql_sanitize(std::vector<std::string>);
    ///@}

    /**
     * The main class for parsing CSV files
     *
     * CSV data can be read in the following ways
     * -# From in-memory strings using feed() and end_feed()
     * -# From CSV files using the multi-threaded read_csv() function
     *
     * All rows are compared to the column names for length consistency
     * - By default, rows that are too short or too long are dropped
     * - A custom callback can be registered by setting bad_row_handler
     */
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
            void clear();
            bool empty();
            void to_json(std::string filename, bool append = false);
            std::vector<std::string> to_json();
            void sample(int n);
            ///@}

            /** @name User-Defined Settings */
            ///@{
            void(*bad_row_handler)(std::vector<std::string>) = nullptr; /**<
                Callback for rows that are too short
                */
            ///@}

            /** @name File Handling */
            ///@{
            std::string infile_name;
            bool eof = false;            /**< Have we reached the end of file */
            void close();          /**< Close the open file handler */
            ///@}

            int row_num = 0;       /**< How many lines have been parsed so far */
            int correct_rows = 0;  /**< How many correct rows (minus header) have been parsed so far */

            CSVReader(
                std::string delim=",",
                std::string quote="\"",
                int header=0,
                std::vector<int> subset_= std::vector<int>{});

            // CSVReader Iterator
            class iterator : public std::iterator <
                std::input_iterator_tag,         // iterator category
                std::vector<std::string>,        // value type
                std::vector<std::string>,        // difference type
                const std::vector<std::string>*, // pointer
                std::vector<std::string>         // reference
            > {
                CSVReader * reader_p;
                std::deque<std::vector<std::string>>::iterator record_it;
                size_t chunk_size;
            public:
                explicit iterator(
                    CSVReader* _ptr,
                    std::deque<std::vector<std::string>>::iterator _it,
                    size_t _chunk_size=ITERATION_CHUNK_SIZE) :
                    reader_p(_ptr), record_it(_it), chunk_size(_chunk_size) {};

                iterator& operator++() {
                    if (this->record_it == reader_p->records.end() && !reader_p->eof)
                        reader_p->read_csv(reader_p->infile_name, ITERATION_CHUNK_SIZE, false);

                    (this->record_it)++;
                    return *this;
                }

                iterator operator++(int) {
                    iterator ret = *this;
                    ++(*this);
                    return ret;
                }

                bool operator==(iterator other) const {
                    return (this->record_it) == (other.record_it);
                }

                bool operator!=(iterator other) const {
                    return !(*this == other);
                }

                reference operator*() const {
                    return *(this->record_it);
                }
            };

            iterator begin() {
                /** Return an iterator over the rows CSVReader has parsed so far */
                return iterator(this, this->records.begin());
            }

            iterator begin(std::string filename, size_t chunk_size=ITERATION_CHUNK_SIZE) {
                /** Return an iterator over a potentially larger-than-RAM file */
                this->read_csv(filename, chunk_size, false);
                return iterator(this, this->records.begin(), chunk_size);
            }

            iterator end() {
                /** Return an iterator pointing the the last parsed row
                 *
                 *  **Note:** If iterating over a file, the row which end() points to will 
                 *  change as more data is read
                 */
                return iterator(this, this->records.end());
            }

        protected:
            // CSV parsing callbacks
            inline void process_possible_delim(std::string&, size_t&, std::string*&);
            inline void process_quote(std::string&, size_t&, std::string*&);
            inline void process_newline(std::string&, size_t&, std::string*&);
            inline void write_record();
            
            // Helper methods
            inline std::string csv_to_json(std::vector<std::string>&);
            
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
            std::FILE* infile = nullptr;
            void _read_csv();                     /**< Worker thread */
            std::deque<std::string*> feed_buffer; /**< Message queue for worker */
            std::mutex feed_lock;                 /**< Allow only one worker to write */
            std::condition_variable feed_cond;    /**< Wake up worker */
            
            // Buffers
            std::deque< std::vector < std::string > > records = { { std::string() } }; /**< Queue of parsed CSV rows */
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
    class CSVWriter {
        public:
            void write_row(std::vector<std::string> record, bool quote_minimal=true);
            void close();
            CSVWriter(std::string filename);
        private:
            std::ofstream outfile;
    };
}