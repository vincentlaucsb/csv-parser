# Vince's CSV Parser
[![CMake on Windows](https://github.com/vincentlaucsb/csv-parser/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/vincentlaucsb/csv-parser/actions/workflows/cmake-multi-platform.yml) [![Memory and Thread Sanitizers](https://github.com/vincentlaucsb/csv-parser/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/vincentlaucsb/csv-parser/actions/workflows/sanitizers.yml)

- [Vince's CSV Parser](#vinces-csv-parser)
  - [Motivation](#motivation)
    - [Performance and Memory Requirements](#performance-and-memory-requirements)
      - [Show me the numbers](#show-me-the-numbers)
      - [Chunk Size Tuning](#chunk-size-tuning)
    - [Robust Yet Flexible](#robust-yet-flexible)
      - [RFC 4180 and Beyond](#rfc-4180-and-beyond)
      - [Encoding](#encoding)
    - [Well Tested](#well-tested)
      - [Bug Reports](#bug-reports)
  - [Documentation](#documentation)
  - [Sponsors](#sponsors)
  - [Integration](#integration)
    - [C++ Version](#c-version)
    - [Single Header](#single-header)
    - [CMake Instructions](#cmake-instructions)
      - [Avoid cloning with FetchContent](#avoid-cloning-with-fetchcontent)
  - [Features \& Examples](#features--examples)
    - [Reading an Arbitrarily Large File (with Iterators)](#reading-an-arbitrarily-large-file-with-iterators)
      - [Memory-Mapped Files vs. Streams](#memory-mapped-files-vs-streams)
    - [Indexing by Column Names](#indexing-by-column-names)
    - [Numeric Conversions](#numeric-conversions)
    - [Converting to JSON](#converting-to-json)
    - [Specifying the CSV Format](#specifying-the-csv-format)
      - [Trimming Whitespace](#trimming-whitespace)
      - [Handling Variable Numbers of Columns](#handling-variable-numbers-of-columns)
      - [Setting Column Names](#setting-column-names)
    - [Parsing an In-Memory String](#parsing-an-in-memory-string)
    - [DataFrames for Random Access and Updates](#dataframes-for-random-access-and-updates)
    - [Writing CSV Files](#writing-csv-files)

## Motivation
There's plenty of other CSV parsers in the wild, but I had a hard time finding what I wanted. Inspired by Python's `csv` module, I wanted a library with **simple, intuitive syntax**. Furthermore, I wanted support for special use cases such as calculating statistics on very large files. Thus, this library was created with these following goals in mind.

### Performance and Memory Requirements
A high performance CSV parser allows you to take advantage of the deluge of large datasets available. By using overlapped threads, memory mapped IO, and 
minimal memory allocation, this parser can quickly tackle large CSV files--even if they are larger than RAM.

In fact, [according to Visual Studio's profier](https://github.com/vincentlaucsb/csv-parser/wiki/Microsoft-Visual-Studio-CPU-Profiling-Results) this
CSV parser **spends almost 90% of its CPU cycles actually reading your data** as opposed to getting hung up in hard disk I/O or pushing around memory.

#### Show me the numbers
On my computer (12th Gen Intel(R) Core(TM) i5-12400 @ 2.50 GHz/Western Digital Blue 5400RPM HDD), this parser can read
 * the [69.9 MB 2015_StateDepartment.csv](https://github.com/vincentlaucsb/csv-data/tree/master/real_data) in 0.19 seconds (360 MBps)
 * a [1.4 GB Craigslist Used Vehicles Dataset](https://www.kaggle.com/austinreese/craigslist-carstrucks-data/version/7) in 1.18 seconds (1.2 GBps)
 * a [2.9GB Car Accidents Dataset](https://www.kaggle.com/sobhanmoosavi/us-accidents) in 8.49 seconds (352 MBps)

#### Chunk Size Tuning

By default, the parser reads CSV data in 10MB chunks. This balance was determined through empirical testing to optimize throughput while minimizing memory overhead and thread synchronization costs.

If you encounter rows larger than the chunk size, use `set_chunk_size()` to adjust:

```cpp
CSVReader reader("massive_rows.csv");
reader.set_chunk_size(100 * 1024 * 1024);  // 100MB chunks
for (auto& row : reader) {
    // Process row
}
```

**Tuning guidance:** The default 10MB provides good balance for typical workloads. Smaller chunks (e.g., 500KB) increase thread overhead without meaningful memory savings. Larger chunks (e.g., 100MB+) reduce thread coordination overhead but consume more memory and delay the first row. Feel free to experiment and measure with your own hardware and data patterns.

### Robust Yet Flexible
#### RFC 4180 and Beyond
This CSV parser is much more than a fancy string splitter, and parses all files following [RFC 4180](https://www.rfc-editor.org/rfc/rfc4180.txt).

However, in reality we know that RFC 4180 is just a suggestion, and there's many "flavors" of CSV such as tab-delimited files. Thus, this library has:
 * Automatic delimiter guessing
 * Ability to ignore comments in leading rows and elsewhere
 * Ability to handle rows of different lengths
 * Ability to handle arbitrary line endings (as long as they are some combination of carriage return and newline)

By default, rows of variable length are silently ignored, although you may elect to keep them or throw an error.

#### Encoding
This CSV parser is encoding-agnostic and will handle ANSI and UTF-8 encoded files.
It does not try to decode UTF-8, except for detecting and stripping UTF-8 byte order marks.

### Well Tested
This CSV parser has:
 * An extensive Catch2 test suite
 * Address, thread safety, and undefined behavior checks with ASan, TSan, and Valgrind (see [GitHub Actions](https://github.com/vincentlaucsb/csv-parser/actions))

#### Bug Reports
Found a bug? Please report it! This project welcomes **genuine bug reports brought in good faith**:
 * ✅ Crashes, memory leaks, data corruption, race conditions
 * ✅ Incorrect parsing of valid CSV files
 * ✅ Performance regressions in real-world scenarios
 * ✅ API issues that affect **practical, real-world use cases**

Please keep reports grounded in real use cases—no contrived edge cases or philosophical debates about API design, thanks!

**Design Note:** `CSVReader` uses `std::input_iterator_tag` for single-pass streaming of arbitrarily large files. If you need multi-pass iteration or random access, copy rows to a `std::vector` first. This is by design, not a bug.

## Documentation

In addition to the [Features & Examples](#features--examples) below, a [fully-fledged online documentation](https://vincela.com/csv/) contains more examples, details, interesting features, and instructions for less common use cases.

## Sponsors
If you use this library for work, please [become a sponsor](https://github.com/sponsors/vincentlaucsb). Your donation
will fund continued maintenance and development of the project.

## Integration

This library was developed with Microsoft Visual Studio and is compatible with >g++ 7.5 and clang.
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

#### Avoid cloning with FetchContent
Don't want to clone? No problem. There's also [a simple example documenting how to use CMake's FetchContent module to integrate this library](https://github.com/vincentlaucsb/csv-parser/wiki/Example:-Using-csv%E2%80%90parser-with-CMake-and-FetchContent).


## Features & Examples
### Reading an Arbitrarily Large File (with Iterators)
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

#### Memory-Mapped Files vs. Streams
By default, passing in a file path string to the constructor of `CSVReader`
causes memory-mapped IO to be used. In general, this option is the most
performant.

However, `std::ifstream` may also be used as well as in-memory sources via `std::stringstream`.

**Note**: Currently CSV guessing only works for memory-mapped files. The CSV dialect
must be manually defined for other sources.

**⚠️ IMPORTANT - Iterator Type and Memory Safety**:  
`CSVReader::iterator` is an **input iterator** (`std::input_iterator_tag`), NOT a forward iterator.
This design enables streaming large CSV files (50+ GB) without loading them entirely into memory.

**Why Forward Iterator Algorithms Don't Work**:
- As the iterator advances, underlying data chunks are automatically freed to bound memory usage
- Algorithms like `std::max_element` require ForwardIterator semantics (multi-pass, hold multiple positions)
- Using such algorithms directly on `CSVReader::iterator` will cause **heap-use-after-free** when the
  algorithm tries to access iterators pointing to already-freed data chunks
- While it may appear to work with small files that fit in a single chunk, it WILL fail with larger files

**✅ Correct Approach for ForwardIterator Algorithms**:
```cpp
// Copy rows to vector first (enables multi-pass iteration)
CSVReader reader("large_file.csv");
std::vector<CSVRow> rows(reader.begin(), reader.end());

// Now safely use any algorithm requiring ForwardIterator
auto max_row = std::max_element(rows.begin(), rows.end(), 
    [](const CSVRow& a, const CSVRow& b) { 
        return a["salary"].get<double>() < b["salary"].get<double>(); 
    });
```


```cpp
CSVFormat format;
// custom formatting options go here

CSVReader mmap("some_file.csv", format);

std::ifstream infile("some_file.csv", std::ios::binary);
CSVReader ifstream_reader(infile, format);

std::stringstream my_csv;
CSVReader sstream_reader(my_csv, format);
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

 * `try_get<T>()` is a non-throwing version of `get<T>` which returns `bool` if the conversion was successful
 * Type checking is performed on conversions to prevent undefined behavior and integer overflow
   * Negative numbers cannot be blindly converted to unsigned integer types
 * `get<float>()`, `get<double>()`, and `get<long double>()` are capable of parsing numbers written in scientific notation.
 * **Note:** Conversions to floating point types are not currently checked for loss of precision.

```cpp
# include "csv.hpp"

using namespace csv;

...

CSVReader reader("very_big_file.csv");

for (auto& row: reader) {
    if (row["timestamp"].is_int()) {
        // Can use get<>() with any integer type, but negative
        // numbers cannot be converted to unsigned types
        row["timestamp"].get<int>();
        
        // You can also attempt to parse hex values
        long long value;
        if (row["hexValue"].try_parse_hex(value)) {
            std::cout << "Hex value is " << value << std::endl;
        }

        // Or specify a different integer type
        int smallValue;
        if (row["smallHex"].try_parse_hex<int>(smallValue)) {
            std::cout << "Small hex value is " << smallValue << std::endl;
        }

        // Non-imperial decimal numbers can be handled this way
        long double decimalValue;
        if (row["decimalNumber"].try_parse_decimal(decimalValue, ',')) {
            std::cout << "Decimal value is " << decimalValue << std::endl;
        }

        // ..
    }
}

```

### Converting to JSON
You can serialize individual rows as JSON objects, where the keys are column names, or as 
JSON arrays (which don't contain column names). The outputted JSON contains properly escaped
strings with minimal whitespace and no quoting for numeric values. How these JSON fragments are 
assembled into a larger JSON document is an exercise left for the user.

```cpp
# include <sstream>
# include "csv.hpp"

using namespace csv;

...

CSVReader reader("very_big_file.csv");
std::stringstream my_json;

for (auto& row: reader) {
    my_json << row.to_json() << std::endl;
    my_json << row.to_json_array() << std::endl;

    // You can pass in a vector of column names to
    // slice or rearrange the outputted JSON
    my_json << row.to_json({ "A", "B", "C" }) << std::endl;
    my_json << row.to_json_array({ "C", "B", "A" }) << std::endl;
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
      .header_row(2);   // Header is on 3rd row (zero-indexed)
      // .no_header();  // Parse CSVs without a header row
      // .quote(false); // Turn off quoting 

// Alternatively, we can use format.delimiter({ '\t', ',', ... })
// to tell the CSV guesser which delimiters to try out

CSVReader reader("wierd_csv_dialect.csv", format);

for (auto& row: reader) {
    // Do stuff with rows here
}

```

#### Trimming Whitespace
This parser can efficiently trim off leading and trailing whitespace. Of course,
make sure you don't include your intended delimiter or newlines in the list of characters
to trim.

```cpp
CSVFormat format;
format.trim({ ' ', '\t'  });
```

#### Handling Variable Numbers of Columns
Sometimes, the rows in a CSV are not all of the same length. Whether this was intentional or not,
this library is built to handle all use cases.

```cpp
CSVFormat format;

// Default: Silently ignoring rows with missing or extraneous columns
format.variable_columns(false); // Short-hand
format.variable_columns(VariableColumnPolicy::IGNORE_ROW);

// Case 2: Keeping variable-length rows
format.variable_columns(true); // Short-hand
format.variable_columns(VariableColumnPolicy::KEEP);

// Case 3: Throwing an error if variable-length rows are encountered
format.variable_columns(VariableColumnPolicy::THROW);
```

#### Setting Column Names
If a CSV file does not have column names, you can specify your own:

```cpp
std::vector<std::string> col_names = { ... };
CSVFormat format;
format.column_names(col_names);
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

### DataFrames for Random Access and Updates

For files that fit comfortably in memory, `DataFrame` provides fast keyed access, in-place updates, and grouping operations—all built on the same high-performance parser.

**Creating a DataFrame with Keyed Access**
```cpp
# include "csv.hpp"

using namespace csv;

...

// Create a DataFrame keyed by employee ID
CSVReader reader("employees.csv");
DataFrame<int> df(reader, "employee_id");

// O(1) lookups by key
auto salary = df[12345]["salary"].get<double>();

// Access by position also works
auto first_row = df[0];
auto name = first_row["name"].get<std::string>();

// Check if a key exists
if (df.contains(99999)) {
    std::cout << "Employee exists" << std::endl;
}
```

**Updating Values**
```cpp
// Updates are stored in an efficient overlay without copying the entire dataset
df.set(12345, "salary", "95000");
df.set(67890, "department", "Engineering");

// Access methods return updated values transparently
std::cout << df[12345]["salary"].get<std::string>(); // "95000"

// Iterate with edits visible
for (auto& row : df) {
    std::cout << row["salary"].get<std::string>(); // Shows edited values
}
```

**Grouping and Analysis**
```cpp
// Group by department
auto groups = df.group_by("department");
for (auto& [dept, row_indices] : groups) {
    double total_salary = 0;
    for (size_t i : row_indices) {
        total_salary += df[i]["salary"].get<double>();
    }
    std::cout << dept << " total: $" << total_salary << std::endl;
}

// Group using a custom function
auto by_salary_range = df.group_by([](const CSVRow& row) {
    double salary = row["salary"].get<double>();
    return salary < 50000 ? "junior" : salary < 100000 ? "mid" : "senior";
});
```

**Writing Back to CSV**
```cpp
// DataFrameRow has implicit conversion for CSVWriter compatibility
auto writer = make_csv_writer(std::cout);
for (auto& row : df) {
    writer << row;  // Outputs edited values
}
```

**When to Use DataFrame vs. CSVReader:**
- **Use CSVReader** for: Large files (>1GB), streaming pipelines, minimal memory footprint
- **Use DataFrame** for: Files that fit in RAM, frequent lookups/updates, grouping operations, data that needs random access

Both options deliver the same parsing performance—DataFrame simply keeps the results in memory for convenience.

### Writing CSV Files

```cpp
# include "csv.hpp"
# include ...

using namespace csv;
using namespace std;

...

stringstream ss; // Can also use ofstream, etc.

auto writer = make_csv_writer(ss);
// auto writer = make_tsv_writer(ss);               // For tab-separated files
// DelimWriter<stringstream, '|', '"'> writer(ss);  // Your own custom format
// set_decimal_places(2);                           // How many places after the decimal will be written for floats

writer << vector<string>({ "A", "B", "C" })
    << deque<string>({ "I'm", "too", "tired" })
    << list<string>({ "to", "write", "documentation." });

writer << array<string, 3>({ "The quick brown", "fox", "jumps over the lazy dog" });
writer << make_tuple(1, 2.0, "Three");
...
```

You can pass in arbitrary types into `DelimWriter` by defining a conversion function
for that type to `std::string`.
