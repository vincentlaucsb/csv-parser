# Scientific Notation Parsing

This library has support for parsing scientific notation through `csv::internals::data_type()`,
which is in turned called by `csv::CSVField::get()` when used with a floating point value type
as the template parameter. Malformed scientific notation will be interpreted by this 
library as a regular string.

## Examples
\snippet tests/test_data_type.cpp Parse Scientific Notation

## Supported Flavors

Many different variations of E-notation are supported, as long as there isn't a whitespace
between E and the successive exponent. As seen below, the `+` sign is optional, and any number of 
zeroes is accepted.

\snippet tests/test_data_type.cpp Scientific Notation Flavors