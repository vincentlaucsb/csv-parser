#pragma once
#include <string>
#include <vector>

namespace csv {
    /**
     *  @brief Stores information about how to parse a CSV file
     *
     *   - Can be used to initialize a csv::CSVReader() object
     *   - The preferred way to pass CSV format information between functions
     *
     *  @see csv::DEFAULT_CSV, csv::GUESS_CSV
     *
     */
    struct CSVFormat {
        char delim;
        char quote_char;

        /**< @brief Row number with columns (ignored if col_names is non-empty) */
        int header;

        /**< @brief Should be left empty unless file doesn't include header */
        std::vector<std::string> col_names;

        /**< @brief RFC 4180 non-compliance -> throw an error */
        bool strict;

        /**< @brief Detect and strip out Unicode byte order marks */
        bool unicode_detect;
    };
}