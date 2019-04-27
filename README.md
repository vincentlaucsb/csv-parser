# Vince's CSV Parser
[![Build Status](https://travis-ci.org/vincentlaucsb/csv-parser.svg?branch=master)](https://travis-ci.org/vincentlaucsb/csv-parser)
[![codecov](https://codecov.io/gh/vincentlaucsb/csv-parser/branch/master/graph/badge.svg)](https://codecov.io/gh/vincentlaucsb/csv-parser)

## Motivation
There's plenty of other CSV parsers in the wild, but I had a hard time finding what I wanted. Specifically, I wanted something which had an interface similar to Python's `csv` module. Furthermore, I wanted support for special use cases such as calculating statistics on very large files. Thus, this library was created with these following goals in mind:

### Performance
This CSV parser uses multiple threads to simulatenously pull data from disk and parse it. Furthermore, it is capable of incremental streaming (parsing larger than RAM files), and quickly parsing data types.

### RFC 4180 Compliance
This CSV parser is much more than a fancy string splitter, and follows every guideline from [RFC 4180](https://www.rfc-editor.org/rfc/rfc4180.txt). On the other hand, it is also robust and capable of handling deviances from the standard. An optional strict parsing mode can be enabled to sniff out errors in files.

#### Encoding
This CSV parser will handle ANSI and UTF-8 encoded files. It does not try to decode UTF-8, except for detecting and stripping byte order marks.

### Easy to Use and [Well-Documented](http://vincela.com/csv)

In additon to being easy on your computer's hardware, this library is also easy on you--the developer. Some helpful features include:
 * Decent ability to guess the dialect of a file (CSV, tab-delimited, etc.)
 * Ability to handle common deviations from the CSV standard, such as inconsistent row lengths, and leading comments
 * Ability to manually set the delimiter and quoting character of the parser

### Well Tested
This CSV parser has an extensive test suite and is checked for memory safety with Valgrind. If you still manage to find a bug,
do not hesitate to report it.

## Building and Compatibility [(latest stable version)](https://github.com/vincentlaucsb/csv-parser/releases)

This library was developed with Microsoft Visual Studio and is compatible with g++ and clang.
All of the code required to build this library, aside from the C++ standard library, is contained under `include/`.

### C++ Version
C++11 is the minimal version required. This library makes extensive use of string views, either through
[Martin Moene's string view library](https://github.com/martinmoene/string-view-lite) or 
`std:string_view` when compiling with C++17. Please be aware of this if you use parts of the public API that
return string views.

### Single Header
This library is available as a single `.hpp` file under `single_include/csv.hpp`. This header includes all necessary 
internal and external dependencies.

### CMake Instructions
If you're including this in another CMake project, you can simply clone this repo into your project directory, 
and add the following to your CMakeLists.txt:

```
include(${CMAKE_SOURCE_DIR}/.../csv-parser/CMakeLists.txt)

# ...

add_executable(<your program> ...)
target_link_libraries(<your program> csv)

```

## Features & Examples
### Reading a Large File (with Iterators)
With this library, you can easily stream over a large file without reading its entirety into memory.

**C++ Style**
```cpp
# include "csv.hpp"

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
# include "csv.hpp"

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
# include "csv.hpp"

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
# include "csv.hpp"
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
# include "csv.hpp"

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
# include "csv.hpp"
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
