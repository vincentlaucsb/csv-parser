# Vince's CSV Parser
[![Build Status](https://travis-ci.org/vincentlaucsb/csv-parser.svg?branch=master)](https://travis-ci.org/vincentlaucsb/csv-parser)
[![codecov](https://codecov.io/gh/vincentlaucsb/csv-parser/branch/master/graph/badge.svg)](https://codecov.io/gh/vincentlaucsb/csv-parser)

## Motivation
There's plenty of other CSV parsers in the wild, but I had a hard time finding what I wanted. Specifically, I wanted something which had an interface similar to Python's `csv` module. Furthermore, I wanted support for special use cases such as calculating statistics on very large files. Thus, this library was created with these following goals in mind:

### Performance
This CSV parser uses multiple threads to simulatenously pull data from disk and parse it. Furthermore, it is capable of incremental streaming (parsing larger than RAM files), and quickly parsing data types.

### RFC 4180 Compliance
This CSV parser is much more than a fancy string splitter, and follows every guideline from [RFC 4180](https://www.rfc-editor.org/rfc/rfc4180.txt). On the other hand, it is also robust and capable of handling deviances from the standard. An optional strict parsing mode can be enabled to sniff out errors in files.

### Easy to Use and [Well-Documented](https://vincentlaucsb.github.io/csv-parser)

In additon to being easy on your computer's hardware, this library is also easy on you--the developer. Some helpful features include:
 * Decent ability to guess the dialect of a file (CSV, tab-delimited, etc.)
 * Ability to handle common deviations from the CSV standard, such as inconsistent row lengths, and leading comments
 * Ability to manually set the delimiter and quoting character of the parser

### Well Tested
In addition to using modern C++ features to build a memory safe parser while still performing well, this parser has a extensive test suite.

## Building [(latest stable version)](https://github.com/vincentlaucsb/csv-parser/releases)

All of this library's essentials are located under `src/`, with no dependencies aside from the STL. This is a C++17 library developed using Microsoft Visual Studio and compatible with g++ and clang. The CMakeList and Makefile contain instructions for building the main library, some sample programs, and the test suite.

**GCC/Clang Compiler Flags**: `-pthread -O3 -std=c++17`

### CMake Instructions
If you're including this in another CMake project, you can simply clone this repo into your project directory, 
and add the following to your CMakeLists.txt:

```
include(${CMAKE_SOURCE_DIR}/.../csv-parser/CMakeLists.txt)

# ...

add_executable(<your program> ...)
target_link_libraries(<your program> csv)

```

## Thirty-Second Introduction to Vince's CSV Parser

* **Parsing CSV Files from..**
  * Files: csv::CSVReader(filename)
  * In-Memory Sources:
    * Small: csv::parse() or csv::operator""_csv();
    * Large: csv::CSVReader::feed();
* **Retrieving Parsed CSV Rows (from CSVReader)**
  * csv::CSVReader::iterator (supports range-based for loop)
  * csv::CSVReader::read_row()
* **Working with CSV Rows**
  * Index by number or name: csv::CSVRow::operator[]()
  * Random access iterator: csv::CSVRow::iterator
  * Conversion: csv::CSVRow::operator std::vector<std::string>();
* **Calculating Statistics**
  * Files: csv::CSVStat(filename)
  * In-Memory: csv::CSVStat::feed()
* **Utility Functions**
  * Return column names: get_col_names()
  * Return the position of a column: get_col_pos();
  * Return column types (for uploading to a SQL database): csv_data_types();

## Features & Examples
### Reading a Large File (with Iterators)
With this library, you can easily stream over a large file without reading its entirety into memory.

**C++ Style**
```cpp
# include "csv_parser.hpp"

using namespace csv;

...

CSVReader reader("very_big_file.csv");

for (CSVRow& row: reader) { // Input iterator
    for (CSVField& field: row) {
        // For efficiency, get<>() produces a string_view
        std::cout << field.get<>() << ...
    }
}

...
```

**Old-Fashioned C Style Loop**
```cpp
...

CSVReader reader("very_big_file.csv");
CSVRow row;
 
while (reader.read_row(row)) {
    // Do stuff with row here
}

...
```

### Indexing by Column Names
Retrieving values using a column name string is a cheap, constant time operation.

```cpp
# include "csv_parser.hpp"

using namespace csv;

...

CSVReader reader("very_big_file.csv");
double sum = 0;

for (auto& row: reader) {
    // Note: Can also use index of column with [] operator
    sum += row["Total Salary"].get<double>();
}

...
```

### Type Conversions
If your CSV has lots of numeric values, you can also have this parser (lazily)
convert them to the proper data type. Type checking is performed on conversions
to prevent undefined behavior.

```cpp
# include "csv_parser.hpp"

using namespace csv;

...

CSVReader reader("very_big_file.csv");

for (auto& row: reader) {
    if (row["timestamp"].is_int()) {
        row["timestamp"].get<int>();
        
        // ..
    }
}

```

### Specifying a Specific Delimiter, Quoting Character, etc.
Although the CSV parser has a decent guessing mechanism, in some cases it is preferrable to specify the exact parameters of a file.

```cpp
# include "csv_parser.hpp"
# include ...

using namespace csv;

CSVFormat format = {
    '\t',    // Delimiter
    '~',     // Quote-character
    '2',     // Line number of header
    {}       // Column names -- if empty, then filled by reading header row
};

CSVReader reader("wierd_csv_dialect.csv", {}, format);

for (auto& row: reader) {
    // Do stuff with rows here
}

```

### Parsing an In-Memory String

```cpp
# include "csv_parser.hpp"

using namespace csv;

...

// Method 1: Using parse()
std::string csv_string = "Actor,Character\r\n"
    "Will Ferrell,Ricky Bobby\r\n"
    "John C. Reilly,Cal Naughton Jr.\r\n"
    "Sacha Baron Cohen,Jean Giard\r\n";

auto rows = parse(csv_string);
for (auto& r: rows) {
    // Do stuff with row here
}
    
// Method 2: Using _csv operator
auto rows = "Actor,Character\r\n"
    "Will Ferrell,Ricky Bobby\r\n"
    "John C. Reilly,Cal Naughton Jr.\r\n"
    "Sacha Baron Cohen,Jean Giard\r\n"_csv;

for (auto& r: rows) {
    // Do stuff with row here
}

```

### Writing CSV Files

```cpp
# include "csv_writer.hpp"
# include ...

using namespace csv;
using vector;
using string;

...

std::stringstream ss; // Can also use ifstream, etc.
auto writer = make_csv_writer(ss);
writer << vector<string>({ "A", "B", "C" })
    << vector<string>({ "I'm", "too", "tired" })
    << vector<string>({ "to", "write", "documentation" });
    
...

```

## Contributing
Bug reports, feature requests, and so on are always welcome. Feel free to leave a note in the Issues section.
