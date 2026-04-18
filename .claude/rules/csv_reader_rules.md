---
paths:
  - "include/internal/csv_reader.hpp"
  - "include/internal/csv_reader.cpp"
  - "include/internal/csv_reader_iterator.cpp"
---

# CSVReader Iterator Rules

## CRITICAL CONSTRAINT: DO NOT Cache All RawCSVDataPtr Chunks

### The Streaming Architecture
- Library parses 50+ GB CSV files with bounded memory (e.g., 8GB RAM)
- Previous data chunks are freed as iterator advances
- `CSVReader::iterator` is `std::input_iterator_tag` by design (single-pass)

### When You See Heap-Use-After-Free with std::max_element or ForwardIterator Algorithms

**❌ WRONG FIX (defeats streaming)**:
```cpp
class iterator {
    std::vector<RawCSVDataPtr> _all_data_chunks;  // NO!
    // Result: 50GB CSV requires 50GB RAM
};
```

**✅ CORRECT FIXES**:
1. **Document limitation**: ForwardIterator algorithms not supported on CSVReader::iterator
2. **Fix test**: Copy to vector first: `std::vector<CSVRow> rows(reader.begin(), reader.end());`
3. **Update README**: Show vector-based workaround for std::max_element

### Valid RawCSVDataPtr Usage
- ✅ `CSVRow::iterator` CAN have RawCSVDataPtr member (single row scope, negligible memory)
- ❌ `CSVReader::iterator` CANNOT cache all chunks (file scope, unbounded memory)

### Root Cause Analysis
- Small test files (< chunk size) fit in one chunk → ForwardIterator algorithms appear to work
- Large files span multiple chunks → heap-use-after-free when algorithm accesses freed chunk
- Solution: Document as unsupported OR provide vector workaround, NOT cache all chunks
