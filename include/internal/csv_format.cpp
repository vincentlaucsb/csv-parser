/** @file
 *  Defines an object used to store CSV format settings
 */

#include "csv_format.hpp"

namespace csv {
    CSVFormat create_default_csv_strict() {
        CSVFormat format;
        format.delimiter(',')
            .quote('"')
            .header_row(0)
            .detect_bom(true)
            .strict_parsing(true);

        return format;
    }

    CSVFormat create_guess_csv() {
        CSVFormat format;
        format.delimiter({ ',', '|', '\t', ';', '^' })
            .quote('"')
            .header_row(0)
            .detect_bom(true);

        return format;
    }

    const CSVFormat CSVFormat::RFC4180_STRICT = create_default_csv_strict();
    const CSVFormat CSVFormat::GUESS_CSV = create_guess_csv();

    CSVFormat& CSVFormat::delimiter(char delim) {
        this->possible_delimiters = { delim };
        return *this;
    }

    CSVFormat& CSVFormat::delimiter(const std::vector<char> & delim) {
        this->possible_delimiters = delim;
        return *this;
    }

    CSVFormat& CSVFormat::quote(char quote) {
        this->quote_char = quote;
        return *this;
    }

    CSVFormat& CSVFormat::column_names(const std::vector<std::string>& names) {
        this->col_names = names;
        this->header = -1;
        return *this;
    }

    CSVFormat& CSVFormat::header_row(int row) {
        this->header = row;
        this->col_names = {};
        return *this;
    }

    CSVFormat& CSVFormat::strict_parsing(bool throw_error) {
        this->strict = throw_error;
        return *this;
    }

    CSVFormat& CSVFormat::detect_bom(bool detect) {
        this->unicode_detect = detect;
        return *this;
    }
}