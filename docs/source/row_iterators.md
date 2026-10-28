@page row_iterators Field Iteration and Lifetimes

# Field Iteration and Lifetimes

`CSVRow` iterators produce `CSVField` values. A field is a small wrapper around
existing CSV text and an independent, lazy conversion cache. Iterating does not
copy the CSV text or allocate a field wrapper on the heap. Retain a field value
when you want to reuse its parsing results:

```cpp
for (auto field : row) {
    const int value = field.get<int>();
    // Subsequent conversions of field reuse its cached classification.
}

auto field = *row.begin();
int value = 0;
if (field.try_get(value)) { /* use value; also works without exceptions */ }
```

Each dereference produces a new wrapper. Changes to one wrapper's conversion
cache, including a custom decimal interpretation, do not affect other wrappers.
Assigning a returned field changes only that temporary value; it does not edit
CSV text. Copy fields into a container if you need a mutable sequence.

## Lifetime

An iterator keeps its original chunk alive and captures its original row span.
It remains attached to that span when the source `CSVRow` is reassigned, moved,
or destroyed. This includes advancing `CSVReader` into later chunks.

A returned `CSVField` does not itself keep the chunk alive. Keep a row or iterator
for that chunk alive while using the field, its string views, or an arrow proxy:

```cpp
auto it = row.begin();
auto field = *it;
++it; // field is still usable: the iterator retains the same chunk.
row = csv::CSVRow(); // The iterator still retains the original row's storage.
```

`it->get<int>()` uses a temporary arrow proxy that owns a field value for the
expression. Its parsing cache is temporary too. Save `auto field = *it` to reuse
conversion results across expressions.

## C++11 through C++17

Forward and reverse field iteration, indexing, arithmetic and arrow syntax work
since C++11. Use `row.rbegin()` and `row.rend()` for reverse traversal.

The legacy `iterator_category` is `std::input_iterator_tag`. The older forward,
bidirectional and random-access requirements demand actual field references;
value-producing iterators cannot claim those contracts. Legacy input algorithms
such as `std::copy`, `std::find_if`, and `std::accumulate` remain supported.
`std::distance` follows the input category and can take linear time; explicit
iterator subtraction is constant time.

Legacy `std::reverse_iterator` is not guaranteed with these field iterators;
use the library's reverse iterator instead. Merely providing decrement and
arithmetic does not satisfy the older standard-library requirements.

## C++20 and later

The same iterators expose `iterator_concept = std::random_access_iterator_tag`
and model the modern random-access concepts. These concepts permit value
results from dereferencing and indexing. Standard reverse adaptors are supported:

```cpp
auto it = std::make_reverse_iterator(row.end());
auto last = *it;

for (auto field : row | std::views::reverse) {
    // Process fields from last to first.
}
```

Read-only ranges algorithms and composed views work with the field values.
Mutation algorithms such as sorting the parsed row in place are not supported.

## Migrating reference-based loops

Replace `for (auto& field : row)` or `for (CSVField& field : row)` with
`for (auto field : row)` or `for (CSVField field : row)`. Likewise, replace
`CSVField& field = *it` with `auto field = *it`.

`const auto&` can extend a temporary field's lifetime, but conversion methods
mutate its local cache. Prefer a non-const field value when converting.

This choice prevents dangling references from temporary iterator copies while
preserving short-lived wrappers and lazy parsing. A persistent chunk-wide cache
would retain touched wrappers and share mutable conversion state across readers.
The value design deliberately avoids that storage and synchronization cost.

`CSVReader` row iteration remains single-pass, with its existing cached row
proxy. Modern random-access support here applies to fields within one row.
