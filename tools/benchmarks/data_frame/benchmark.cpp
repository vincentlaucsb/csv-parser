#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "csv.hpp"

using Clock = std::chrono::steady_clock;
static volatile std::uint64_t sink = 0;

template<typename Fn>
void measure(const char* name, Fn fn) {
    const auto start = Clock::now();
    const std::uint64_t sum = fn();
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    sink = sum;
    std::cout << name << ',' << std::fixed << std::setprecision(6) << ms << ',' << sum << '\n';
}

void scan(const char* indexed_name, const char* iterator_name, const csv::DataFrame<>& frame) {
    const auto column = frame.column_view("name");
    measure(indexed_name, [&]() {
        std::uint64_t sum = 0;
        for (size_t repeat = 0; repeat < 12; ++repeat)
            for (size_t i = 0; i < column.size(); ++i)
                sum += column.get_sv(i).size();
        return sum;
    });
    measure(iterator_name, [&]() {
        std::uint64_t sum = 0;
        for (size_t repeat = 0; repeat < 4; ++repeat)
            for (auto it = column.begin(), end = column.end(); it != end; ++it)
                sum += it->get_sv().size();
        return sum;
    });
}

int main() {
    std::ostringstream generated;
    generated << "id,name,value,quoted\n";
    for (size_t i = 0; i < 500001; ++i)
        generated << i << ",person_" << i << ',' << i * 7 << ",\"quoted_" << i << "_\"\"x\"\"\"\n";
    const std::string text = generated.str();
    std::istringstream input(text);
    csv::CSVReader reader(input);
    std::vector<csv::CSVRow> rows(reader.begin(), reader.end());
    csv::DataFrame<> clean(rows);
    csv::DataFrame<> dirty(rows);
    for (size_t i = 0; i < dirty.size(); i += 499)
        dirty.at(i)[1] = csv::string_view("edited_person");
    csv::DataFrame<> mapped(rows);
    mapped.column_view("id").erase();
    for (size_t i = 0; i < mapped.size(); i += 499)
        mapped.at(i)[0] = csv::string_view("edited_person");
    std::cout << "layout_dfc," << sizeof(csv::DataFrameColumn<std::string>) << ",0\n";
    scan("clean_indexed", "clean_iterator", clean);
    const auto quoted_column = static_cast<const csv::DataFrame<>&>(clean).column_view("quoted");
    measure("clean_quoted_indexed", [&]() {
        std::uint64_t sum = 0;
        for (size_t repeat = 0; repeat < 12; ++repeat)
            for (size_t i = 0; i < quoted_column.size(); ++i)
                sum += quoted_column.get_sv(i).size();
        return sum;
    });
    scan("dirty_indexed", "dirty_iterator", dirty);
    scan("mapped_indexed", "mapped_iterator", mapped);
    csv::DataFrameExecutor executor(2);
    measure("chunk_parallel", [&]() {
        std::istringstream chunk_input(text);
        csv::CSVReader chunk_reader(chunk_input);
        std::vector<std::uint64_t> states(4, 0);
        csv::chunk_parallel_apply(chunk_reader, executor, states,
            [](csv::DataFrame<>::column_type column, std::uint64_t& state) {
                for (size_t i = 0; i < column.size(); ++i)
                    state += column.get_sv(i).size();
            }, 50000);
        std::uint64_t sum = 0;
        for (const auto value : states) sum += value;
        return sum;
    });
    return sink == 0 ? 1 : 0;
}
