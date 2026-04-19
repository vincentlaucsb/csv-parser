/** @file
 *  Defines an object used to store CSV format settings
 */

#include <algorithm>
#include <set>

#include "csv_format.hpp"

namespace csv {
    CSV_INLINE CSVFormat& CSVFormat::delimiter(char delim) {
        this->possible_delimiters = { delim };
        this->assert_no_char_overlap();
        return *this;
    }

    CSV_INLINE CSVFormat& CSVFormat::delimiter(const std::vector<char> & delim) {
        this->possible_delimiters = delim;
        this->assert_no_char_overlap();
        return *this;
    }

    CSV_INLINE CSVFormat& CSVFormat::quote(char quote) {
        this->no_quote = false;
        this->quote_char = quote;
        this->assert_no_char_overlap();
        return *this;
    }

    CSV_INLINE CSVFormat& CSVFormat::trim(const std::vector<char> & chars) {
        this->trim_chars = chars;
        this->assert_no_char_overlap();
        return *this;
    }

    CSV_INLINE CSVFormat& CSVFormat::column_names(const std::vector<std::string>& names) {
        this->col_names = names;
        this->header = -1;
        this->col_names_explicitly_set_ = true;
        return *this;
    }

    CSV_INLINE CSVFormat& CSVFormat::header_row(int row) {
        if (row < 0) this->variable_column_policy = VariableColumnPolicy::KEEP;

        this->header = row;
        this->header_explicitly_set_ = true;
        this->col_names = {};
        this->col_names_explicitly_set_ = false;
        return *this;
    }

    CSV_INLINE CSVFormat& CSVFormat::chunk_size(size_t size) {
        if (size < internals::CSV_CHUNK_SIZE_FLOOR) {
            throw std::invalid_argument(
                "Chunk size must be at least " +
                std::to_string(internals::CSV_CHUNK_SIZE_FLOOR) +
                " bytes (500KB). Provided: " + std::to_string(size)
            );
        }
        this->_chunk_size = size;
        return *this;
    }

    CSV_INLINE void CSVFormat::assert_no_char_overlap()
    {
        const std::set<char> delims(this->possible_delimiters.begin(), this->possible_delimiters.end());
        const std::set<char> trims(this->trim_chars.begin(), this->trim_chars.end());
        std::set<char> offenders;

        for (std::set<char>::const_iterator it = delims.begin(); it != delims.end(); ++it) {
            if (trims.find(*it) != trims.end()) {
                offenders.insert(*it);
            }
        }

        // Make sure quote character is not contained in possible delimiters
        // or whitespace characters.
        if (delims.find(this->quote_char) != delims.end() ||
            trims.find(this->quote_char) != trims.end()) {
            offenders.insert(this->quote_char);
        }

        if (!offenders.empty()) {
            std::string err_msg = "There should be no overlap between the quote character, "
                "the set of possible delimiters "
                "and the set of whitespace characters. Offending characters: ";

            // Create a pretty error message with the list of overlapping
            // characters
            size_t i = 0;
            for (std::set<char>::const_iterator it = offenders.begin(); it != offenders.end(); ++it, ++i) {
                err_msg += "'";
                err_msg += *it;
                err_msg += "'";

                if (i + 1 < offenders.size())
                    err_msg += ", ";
            }

            throw std::runtime_error(err_msg + '.');
        }
    }
}