#pragma once
#include <string>
#include <vector>

namespace csv {
    class CSVReader;

    /**
     *  @brief Stores information about how to parse a CSV file
     *
     *   - Can be used to initialize a csv::CSVReader() object
     *   - The preferred way to pass CSV format information between functions
     *
     */
    class CSVFormat {
    public:
        /** Settings for parsing a RFC 4180 CSV file */
        CSVFormat() = default;

        CSVFormat& delimiter(char delim) {
            this->possible_delimiters = { delim };
            return *this;
        }

        CSVFormat& delimiter(const std::vector<char> & delim) {
            this->possible_delimiters = delim;
            return *this;
        }

        CSVFormat& quote(char quote) {
            this->quote_char = quote;
            return *this;
        }

        CSVFormat& column_names(const std::vector<std::string>& col_names) {
            this->col_names = col_names;
            this->header = -1;
            return *this;
        }

        CSVFormat& header_row(int row) {
            this->header = row;
            this->col_names = {};
            return *this;
        }

        CSVFormat& strict_parsing(bool strict = true) {
            this->strict = strict;
            return *this;
        }

        CSVFormat& detect_bom(bool detect = true) {
            this->unicode_detect = detect;
            return *this;
        }

        char get_delim() {
            // This error should never be received by end users.
            if (this->possible_delimiters.size() > 1) {
                throw std::runtime_error("There is more than one possible delimiter.");
            }

            return this->possible_delimiters.at(0);
        }

        int get_header() {
            return this->header;
        }

        static const CSVFormat GUESS_CSV;
        static const CSVFormat RFC4180_STRICT;

        friend CSVReader;
    private:
        bool guess_delim() {
            return this->possible_delimiters.size() > 1;
        }

        std::vector<char> possible_delimiters = { ',' };

        char quote_char = '"';

        /**< @brief Row number with columns (ignored if col_names is non-empty) */
        int header = 0;

        /**< @brief Should be left empty unless file doesn't include header */
        std::vector<std::string> col_names = {};

        /**< @brief RFC 4180 non-compliance -> throw an error */
        bool strict = false;

        /**< @brief Detect and strip out Unicode byte order marks */
        bool unicode_detect = true;
    };
}