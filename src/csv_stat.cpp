#include "csv_parser.h"

using std::unordered_map;
using std::vector;
using std::string;

namespace csv {
    /** @file
      * Calculates statistics from CSV files
      */

    CSVStat::CSVStat(std::string filename, std::vector<int> subset,
        StatsOptions options, CSVFormat format) : CSVReader(filename, subset, format) {
        /** Lazily calculate statistics for a potentially very big file. */
        while (!this->eof()) {
            this->read_csv(filename, ITERATION_CHUNK_SIZE, false);
            this->calc(options);
        }

        if (!this->records.empty())
            this->calc(options);
    }

    void CSVStat::end_feed() {
        CSVReader::end_feed();
        this->calc();
    }

    std::vector<long double> CSVStat::get_mean() {
        /** Return current means */
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->rolling_means[i]);
        }
        return ret;
    }

    std::vector<long double> CSVStat::get_variance() {
        /** Return current variances */
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->rolling_vars[i]/(this->n[i] - 1));
        }
        return ret;
    }

    std::vector<long double> CSVStat::get_mins() {
        /** Return current variances */
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->mins[i]);
        }
        return ret;
    }

    std::vector<long double> CSVStat::get_maxes() {
        /** Return current variances */
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->maxes[i]);
        }
        return ret;
    }

    std::vector< std::unordered_map<std::string, int> > CSVStat::get_counts() {
        /** Get counts for each column */
        vector<unordered_map<string, int>> ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->counts[i]);
        }
        return ret;
    }

    std::vector< std::unordered_map<int, int> > CSVStat::get_dtypes() {
        /** Get data type counts for each column */
        std::vector< std::unordered_map<int, int> > ret;        
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            ret.push_back(this->dtypes[i]);
        }
        return ret;
    }

    void CSVStat::calc(StatsOptions options) {
        /** Go through all records and calculate specified statistics */
        for (size_t i = 0; i < this->subset_col_names.size(); i++) {
            dtypes.push_back({});
            counts.push_back({});
            rolling_means.push_back(0);
            rolling_vars.push_back(0);
            mins.push_back(NAN);
            maxes.push_back(NAN);
            n.push_back(0);
        }

        vector<std::thread> pool;

        // Start threads
        for (size_t i = 0; i < subset_col_names.size(); i++)
            pool.push_back(std::thread(&CSVStat::calc_col, this, i));

        // Block until done
        for (auto& th: pool)
            th.join();

        this->clear();
    }

    void CSVStat::calc_col(size_t i) {
        /** Worker thread which calculates statistics for one column.
         *  Intended to be called from CSVStat::calc().
         * 
         *  @param[out] i Column index
         */

        std::deque<vector<string>>::iterator current_record = this->records.begin();
        long double x_n;

        for (size_t processed = 0; current_record != this->records.end(); processed++) {
            // Optimization: Don't count() if there's too many distinct values in the first 1000 rows
            if (processed < 1000 || this->counts[i].size() <= 500)
                this->count((*current_record)[i], i);

            auto current_dtype = this->dtype((*current_record)[i], i, x_n);

            // Numeric Stuff
            try {
                // Using data_type() to check if field is numeric is faster
                // than catching stold() errors
                if (current_dtype >= CSV_INT) {
                    // This actually calculates mean AND variance
                    this->variance(x_n, i);
                    this->min_max(x_n, i);
                }
            }
            catch (std::out_of_range) {
                // Ignore for now
            }

            ++current_record;
        }
    }

    DataType CSVStat::dtype(std::string &record, const size_t &i, long double &x_n) {
        /** Given a record update the type counter and for efficiency, return
         *  the results of data_type()
         *  @param[in]  record Data observation
         *  @param[out] i      The column index that should be updated
         *  @param[out] x_n    Stores the resulting of parsing record
         */
        DataType type = helpers::data_type(record, &x_n);
        
        if (this->dtypes[i].find(type) !=
            this->dtypes[i].end()) {
            // Increment count
            this->dtypes[i][type]++;
        } else {
            // Initialize count
            this->dtypes[i].insert(std::make_pair(type, 1));
        }

        return type;
    }

    void CSVStat::count(std::string &record, size_t &i) {
        /** Given a record update the frequency counter
         *  @param[in]  record Data observation
         *  @param[out] i      The column index that should be updated
         */
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
        /** Update current minimum and maximum
         *  @param[in]  x_n Data observation
         *  @param[out] i   The column index that should be updated
         */
        if (isnan(this->mins[i]))
            this->mins[i] = x_n;
        if (isnan(this->maxes[i]))
            this->maxes[i] = x_n;
        
        if (x_n < this->mins[i])
            this->mins[i] = x_n;
        else if (x_n > this->maxes[i])
            this->maxes[i] = x_n;
    }

    void CSVStat::variance(long double &x_n, size_t &i) {
        /** Given a record update rolling mean and variance for all columns
         *  using Welford's Algorithm
         *  @param[in]  x_n Data observation
         *  @param[out] i   The column index that should be updated
         */
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
}