#include "csv.hpp"
#include <chrono>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdint>
static volatile std::uint64_t sink;
using Column=csv::DataFrameColumn<std::string>;
template<class Fn> void measure(const char* name,Fn fn){auto start=std::chrono::steady_clock::now();auto sum=fn(); sink=sum;std::cout<<name<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<','<<sum<<'\n';}
int main(){std::istringstream input("id,name,value\n1,person,99\n");csv::CSVReader reader(input);csv::DataFrame<> frame(reader);const size_t N=500000;std::vector<Column> columns; columns.reserve(N); std::vector<Column> copies;copies.reserve(N);
measure("construct",[&](){for(size_t rep=0;rep<12;++rep){columns.clear();for(size_t i=0;i<N;++i)columns.emplace_back(&frame,1);}std::uint64_t sum=0;for(const auto& column:columns)sum+=column.get_sv(0).size();return sum;});
measure("copy",[&](){for(size_t rep=0;rep<12;++rep){copies.clear();for(const auto& column:columns)copies.push_back(column);}std::uint64_t sum=0;for(const auto& column:copies)sum+=column.get_sv(0).size();return sum;});return 0;}
