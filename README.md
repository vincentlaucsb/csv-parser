# Yet Another... CSV Parser
[![Build Status](https://travis-ci.org/vincentlaucsb/csv-parser.svg?branch=master)](https://travis-ci.org/vincentlaucsb/csv-parser)
[![codecov](https://codecov.io/gh/vincentlaucsb/csv-parser/branch/master/graph/badge.svg)](https://codecov.io/gh/vincentlaucsb/csv-parser)

## Why?
There's plenty of other CSV parsers in the wild, but I had a hard time 
finding what I wanted. So I created this library with these goals in mind:
 * Reasonable performance
 * Full RFC 4180 compliance
 * Well tested
 * [Well documented](http://vincela.com/csv-parser/)
 
Moreover, a CSV parser by itself is pretty boring--it's what you do with the data that matters.
I wanted a parser that was **flexible** and **extensible** with a simple API. Keep reading 
for a list of extra features built into the library, or read the documentation to discover
ways to extend it.

## Building
Incorporating this CSV parser into your project only requires a compiler that speaks C++11, such as g++, clang++, or Microsoft Visual C++.

**Protip:** Use the `-O3` flag for g++/clang++ and `-Ox` flag for MSVC for faster parsing
 
## Features
### Incremental Streaming
CSV data can be read directly from a file, or fed in piecewise via strings.

### CSV Output
Since the parser also works with other delimiters, you can use this to convert--for example--
tab separated values files to CSVs. Furthermore, you can also use this to clean up 
existing CSV files. The parser automatically ensures all rows have a consistent length,
and has options to change how fields are quoted.

### JSON Output
CSV data can be converted into newline-delimited JSON, with automatic RFC 7159 compliant escaping.

### Statistics
With this library, you can compute the mean, variance, and other statistics, as well as build
frequency counters. Because online algorithms are used, there's no need to keep an entire
4GB CSV in memory.
   
#### Data Type Inference
This library can quickly scan a CSV file and report back on what data types are found in 
each column (while simultaneously cleaning it). This is very useful for copying files to
SQL databases.