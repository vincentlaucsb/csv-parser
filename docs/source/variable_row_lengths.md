# Handling Variable Numbers of Columns
Sometimes, the rows in a CSV are not all of the same length. Whether this was intentional or not, this library is built to handle all use cases.

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