#pragma once

/*
Compatibility shim for legacy automation that still reads single_include/csv.hpp.

The full amalgamated header is no longer committed to this repository.
Use the canonical generated file from GitHub Pages instead:

  https://vincentlaucsb.github.io/csv-parser/csv.hpp

Why this file exists:
- Preserve the legacy path for transition/migration tooling
- Provide a deterministic pointer to the canonical distribution URL

This file intentionally does not forward/include any in-repo headers.
*/

#ifndef CSV_PARSER_SINGLE_INCLUDE_SHIM_HPP
#define CSV_PARSER_SINGLE_INCLUDE_SHIM_HPP

#error "single_include/csv.hpp is now a compatibility shim. Download the full header from https://vincentlaucsb.github.io/csv-parser/csv.hpp"

#endif
