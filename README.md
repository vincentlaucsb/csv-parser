# Vince's CSV Parser
[![Build Status](https://travis-ci.org/vincentlaucsb/csv-parser.svg?branch=master)](https://travis-ci.org/vincentlaucsb/csv-parser)
[![codecov](https://codecov.io/gh/vincentlaucsb/csv-parser/branch/master/graph/badge.svg)](https://codecov.io/gh/vincentlaucsb/csv-parser)

 * [Motivation](#motivation)
 * [Documentation](#documentation)
 * [Integration](#integration)
   * [C++ Version](#c-version)
   * [Single Header](#single-header)
   * [CMake Instructions](#cmake-instructions)
 * [Features & Examples](#features--examples)
   * [Reading a Large File (with Iterators)](#reading-a-large-file-with-iterators)
   * [Indexing by Column Names](#indexing-by-column-names)
   * [Numeric Conversions](#numeric-conversions)
   * [Specifying the CSV Format](#specifying-the-csv-format)
      * [Setting Column Names](#setting-column-names)
   * [Parsing an In-Memory String](#parsing-an-in-memory-string)
   * [Writing CSV Files](#writing-csv-files)
 * [Contributing](#contributing)

## Motivation
There's plenty of other CSV parsers in the wild, but I had a hard time finding what I wanted. Inspired by Python's `csv` module, I wanted a library with **simple, intuitive syntax**. Furthermore, I wanted support for special use cases such as calculating statistics on very large files. Thus, this library was created with these following goals in mind.

### Performance
This CSV parser uses multiple threads to simulatenously pull data from disk and parse it. Furthermore, it is capable of incremental streaming (parsing larger than RAM files), and quickly parsing data types.

#### Show me the numbers
*(To be expanded)*

On my computer (Intel Core i7-8550U @ 1.80GHz/Toshiba XG5 SSD), it is capable of parsing the [69.9 MB 2015_StateDepartment.csv](https://github.com/vincentlaucsb/csv-data/tree/master/real_data) in 0.33 seconds.

### Robust
#### RFC 4180 Compliance
This CSV parser is much more than a fancy string splitter, and follows every guideline from [RFC 4180](https://www.rfc-editor.org/rfc/rfc4180.txt). An optional strict parsing mode can be enabled to sniff out errors in files.

#### Non-RFC 4180 Deviations
We know that actual CSV files come with many different quirks. In addition, there are many CSV-inspired formats like tab-separated values. Thus, this CSV library has many features for dealing with this reality:
 * Automatic delimiter guessing
 * Ability to ignore comments in leading rows and elsewhere
 * Ability to handle rows of different lengths

#### Encoding
This CSV parser will handle ANSI and UTF-8 encoded files. It does not try to decode UTF-8, except for detecting and stripping byte order marks.

### Well Tested
This CSV parser has an extensive test suite and is checked for memory safety with Valgrind. If you still manage to find a bug,
do not hesitate to report it.

## Documentation

In addition to the [Features & Examples](#features--examples) below, a [fully-fledged online documentation](http://vincela.com/csv) contains more examples, details, interesting features, and instructions for less common use cases.

## Integration

This library was developed with Microsoft Visual Studio and is compatible with g++ and clang.
All of the code required to build this library, aside from the C++ standard library, is contained under `include/`.

### C++ Version
While C++17 is recommended, C++11 is the minimum version required. This library makes extensive use of string views, and uses
[Martin Moene's string view library](https://github.com/martinmoene/string-view-lite) if `std::string_view` is not available.

### Single Header
This library is available as a single `.hpp` file under [`single_include/csv.hpp`](single_include/csv.hpp).

### CMake Instructions
If you're including this in another CMake project, you can simply clone this repo into your project directory, 
and add the following to your CMakeLists.txt:

```
# Optional: Defaults to C++ 17
# set(CSV_CXX_STANDARD 11)
add_subdirectory(csv-parser)

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
        // By default, get<>() produces a std::string.
        // A more efficient get<string_view>() is also available, where the resulting
        // string_view is valid as long as the parent CSVRow is alive
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

### Numeric Conversions
If your CSV has lots of numeric values, you can also have this parser (lazily)
convert them to the proper data type.

 * Type checking is performed on conversions to prevent undefined behavior and integer overflow.
 * `get<float>()`, `get<double>()`, and `get<long double>()` are capable of parsing numbers written in scientific notation.
 * **Note:** Conversions to floating point types are not currently checked for loss of precision.

```cpp
# include "csv.hpp"

using namespace csv;

...

CSVReader reader("very_big_file.csv");

for (auto& row: reader) {
    if (row["timestamp"].is_int()) {
		// Can use get<>() with any signed integer type
        row["timestamp"].get<int>();
        
        // ..
    }
}

```

### Specifying the CSV Format
Although the CSV parser has a decent guessing mechanism, in some cases it is preferrable to specify the exact parameters of a file.

```cpp
# include "csv.hpp"
# include ...

using namespace csv;

CSVFormat format;
format.delimiter('\t')
      .quote('~')
      .header_row(2);  // Header is on 3rd row (zero-indexed)

// Alternatively, we can use format.delimiter({ '\t', ',', ... })
// to tell the CSV guesser which delimiters to try out

CSVReader reader("wierd_csv_dialect.csv", format);

for (auto& row: reader) {
    // Do stuff with rows here
}

```

#### Setting Column Names
If a CSV file does not have column names, you can specify your own:

```cpp
std::vector<std::string> col_names = { ... };
CSVFormat format;
format.set_column_names(col_names);
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
using namespace std;

...

stringstream ss; // Can also use ifstream, etc.
auto writer = make_csv_writer(ss);
writer << vector<string>({ "A", "B", "C" })
    << deque<string>({ "I'm", "too", "tired" })
    << list<string>({ "to", "write", "documentation" });
    
...

```

## Contributing
Bug reports, feature requests, and so on are always welcome. Feel free to leave a note in the Issues section.
