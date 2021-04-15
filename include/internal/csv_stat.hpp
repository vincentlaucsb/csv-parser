/** @file
 *  Calculates statistics from CSV files
 */

#pragma once
#include <unordered_map>
#include <sstream>
#include <vector>
#include "csv_reader.hpp"

namespace csv {
    /** Class for calculating statistics from CSV files and in-memory sources
     *
     *  **Example**
     *  \include programs/csv_stats.cpp
     *
     */
    class CSVStat {
    public:
        using FreqCount = std::unordered_map<std::string, size_t>;
        using TypeCount = std::unordered_map<DataType, size_t>;

        std::vector<long double> get_mean() const;
        std::vector<long double> get_variance() const;
        std::vector<long double> get_mins() const;
        std::vector<long double> get_maxes() const;
        std::vector<FreqCount> get_counts() const;
        std::vector<TypeCount> get_dtypes() const;

        std::vector<std::string> get_col_names() const {
            return this->reader.get_col_names();
        }

        CSVStat(csv::string_view filename, CSVFormat format = CSVFormat::guess_csv());
        CSVStat(std::stringstream& source, CSVFormat format = CSVFormat());
    private:
        // An array of rolling averages
        // Each index corresponds to the rolling mean for the column at said index
        std::vector<long double> rolling_means;
        std::vector<long double> rolling_vars;
        std::vector<long double> mins;
        std::vector<long double> maxes;
        std::vector<FreqCount> counts;
        std::vector<TypeCount> dtypes;
        std::vector<long double> n;

        // Statistic calculators
        void variance(const long double&, const size_t&);
        void count(CSVField&, const size_t&);
        void min_max(const long double&, const size_t&);
        void dtype(CSVField&, const size_t&);

        void calc();
        void calc_chunk();
        void calc_worker(const size_t&);

        CSVReader reader;
        std::deque<CSVRow> records = {};
    };
}