# Vince's CSV Parser
[![Build Status](https://travis-ci.org/vincentlaucsb/csv-parser.svg?branch=master)](https://travis-ci.org/vincentlaucsb/csv-parser)
[![codecov](https://codecov.io/gh/vincentlaucsb/csv-parser/branch/master/graph/badge.svg)](https://codecov.io/gh/vincentlaucsb/csv-parser)

## Motivation
There's plenty of other CSV parsers in the wild, but I had a hard time finding what I wanted. Specifically, I wanted something which had an interface similar to Python's `csv` module. Furthermore, I wanted support for special use cases such as calculating statistics on very large files. Thus, this library was created with these following goals in mind:

### Performance
This CSV parser uses multiple threads to simulatenously pull data from disk and parse it. Furthermore, it is capable of incremental streaming (parsing larger than RAM files), and quickly parsing data types.

### RFC 4180 Compliance
This CSV parser is much more than a fancy string splitter, and follows every guideline from [RFC 4180](https://www.rfc-editor.org/rfc/rfc4180.txt). On the other hand, it is also robust and capable of handling deviances from the standard. An optional strict parsing mode can be enabled to sniff out errors in files.

### Easy to Use and Well-Documented
https://vincentlaucsb.github.io/csv-parser

In additon to being easy on your computer's hardware, this library is also easy on you--the developer. Some helpful features include:
 * Decent ability to guess the dialect of a file (CSV, tab-delimited, etc.)
 * Ability to handle common deviations from the CSV standard, such as inconsistent row lengths, and leading comments
 * Ability to manually set the delimiter and quoting character of the parser

### Well Tested

## Building
All of this library's essentials are located under `src/`, with no dependencies aside from the STL. This is a C++11 library developed using Microsoft Visual Studio and compatible with g++ and clang. The CMakeList and Makefile contain instructions for building the main library, some sample programs, and the test suite.

**GCC/Clang Compiler Flags**: `-pthread-O3 -std=c++11`

## Features & Examples
### Reading a Large File
With this library, you can easily stream over a large file without reading its entirety into memory.

```
# include "csv_parser.h"

using namespace csv;

...

CSVReader reader("very_big_file.csv");
std::vector<std::string> row;

while (reader.read_row(row)) {
    // Do stuff with row here
}

```

### Reordering/Subsetting Data
You can also reorder a CSV or only keep a subset of the data simply by passing
in a vector of column indices.

```
# include "csv_parser.h"

using namespace csv;

...

std::vector<size_t> new_order = { 0, 2, 3, 5 };
CSVReader reader("very_big_file.csv", new_order);
std::vector<std::string> row;

while (reader.read_row(row)) {
    // Do stuff with row here
}

```

### Automatic Type Conversions
If your CSV has lots of numeric values, you can also have this parser automatically convert them to the proper data type.

```
# include "csv_parser.h"

using namespace csv;

...

CSVReader reader("very_big_file.csv");
std::vector<CSVField> row;

size_t date = reader.index_of("timestamp");

while (reader.read_row(row)) {
    if (row[date].is_int())
        row[date].get<int>();
    
    // get<std::string>() can be called on any values
    std::cout << row[date].get<std::string>() << std::endl;
}

```

### Specifying a Specific Delimiter, Quoting Character, etc.
Although the CSV parser has a decent guessing mechanism, in some cases it is preferrable to specify the exact parameters of a file.

```
# include "csv_parser.h"
# include ...

using namespace csv;

CSVFormat format = {
    '\t',    // Delimiter
    '~',     // Quote-character
    '2',     // Line number of header
    {}       // Column names -- if empty, then filled by reading header row
};

CSVReader reader("wierd_csv_dialect.csv", {}, format);
vector<CSVField> row;

while (reader.read_row(row)) {
    // Do stuff with rows here
}

```

### Parsing an In-Memory String

```
# include "csv_parser.h"
# include ...

using namespace csv;

int main() { 
    std::string csv_string = "Actor,Character"
        "Will Ferrell,Ricky Bobby\r\n"
        "John C. Reilly,Cal Naughton Jr.\r\n"
        "Sacha Baron Cohen,Jean Giard\r\n"

    // Method 1
    std::deque<CSVRow> rows = parse(csv_string);
    for (auto& r: rows) {
        // Do stuff with row here
    }
    
    // Method 2
    std::deque< std::vector<std::string> > rows = parse(csv_string);
    for (auto& r: rows) {
        // Do stuff with row here
    }
    
    // ..
}
```

### Writing CSV Files

```
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


### Utility Functions
 * **Return column names:** get_col_names()
 * **Return the position of a column:** get_col_pos();

## Contributing
Bug reports, feature requests, and so on are always welcome. Feel free to leave a note in the Issues section.
