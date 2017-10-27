# Yet Another... CSV Parser
[![Build Status](https://travis-ci.org/vincentlaucsb/csv-parser.svg?branch=master)](https://travis-ci.org/vincentlaucsb/csv-parser)
[![Coverage Status](https://coveralls.io/repos/github/vincentlaucsb/csv-parser/badge.svg?branch=master)](https://coveralls.io/github/vincentlaucsb/csv-parser?branch=master)

A CSV parser that can handle any RFC 4180 compliant files and then some. Has extra features to calculate statistics, clean up CSVs, and ease loading to SQL.

## Dependencies
This CSV parser only requires a C++11 compliant compiler. Tested on:
 * g++
 * clang
 * Microsoft Visual C++

## Features
### Incremental Streaming
 * CSV files can be read directly from a file or streamed
 * The parser can piece together incomplete CSV fragments
   * Once it encounters a record separator (default: `\r\n`), it parses what it's collected so far

### Statistics
 * Calculates mean, variance, min, max, and counts using online algorithms
   * I.e. you can calculate statistics for arbitrarily large CSV files, even those larger than memory
   
#### Data Type Statistics
 * Can produce a data type count for each column, differentiating between NULL, string, integer, and floating point values
   * Useful for loading CSV files to SQL
