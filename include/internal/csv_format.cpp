/** @file
 *  Defines an object used to store CSV format settings
 */

#include <algorithm>
#include <set>

#include "csv_format.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE char resolved_delimiter(const CSVFormat& format) noexcept {
            CSV_DEBUG_ASSERT(format.possible_delimiters.size() == 1);
            return format.possible_delimiters.front();
        }
    }

#if CSV_EXCEPTIONS_ENABLED
    CSV_INLINE CSVFormat& CSVFormat::delimiter(char delim) {
        CSVError error;
        this->try_delimiter(delim, error);
        internals::throw_error(error);
        return *this;
    }

    CSV_INLINE CSVFormat& CSVFormat::delimiter(const std::vector<char>& delims) {
        CSVError error;
        this->try_delimiter(delims, error);
        internals::throw_error(error);
        return *this;
    }

    CSV_INLINE CSVFormat& CSVFormat::quote(char quote) {
        CSVError error;
        this->try_quote(quote, error);
        internals::throw_error(error);
        return *this;
    }

    CSV_INLINE CSVFormat& CSVFormat::trim(const std::vector<char>& whitespace) {
        CSVError error;
        this->try_trim(whitespace, error);
        internals::throw_error(error);
        return *this;
    }
#endif

#if CSV_EXCEPTIONS_ENABLED
    CSV_INLINE char CSVFormat::get_delim() const {
        CSVError error;
        char value = 0;
        this->try_get_delim(value, error);
        internals::throw_error(error);
        return value;
    }
#endif

    CSV_INLINE bool CSVFormat::try_delimiter(char delim, CSVError& error) {
        return this->try_delimiter(std::vector<char>{delim}, error);
    }

    CSV_INLINE bool CSVFormat::try_delimiter(const std::vector<char>& delims, CSVError& error) {
        if (!validate_characters(delims, this->trim_chars, this->quote_char, error)) return false;
        this->possible_delimiters = delims;
        return true;
    }

    CSV_INLINE bool CSVFormat::try_quote(char quote, CSVError& error) {
        if (!validate_characters(this->possible_delimiters, this->trim_chars, quote, error)) return false;
        this->no_quote = false;
        this->quote_char = quote;
        return true;
    }

    CSV_INLINE bool CSVFormat::try_trim(const std::vector<char>& whitespace, CSVError& error) {
        if (!validate_characters(this->possible_delimiters, whitespace, this->quote_char, error)) return false;
        this->trim_chars = whitespace;
        return true;
    }

    CSV_INLINE bool CSVFormat::try_get_delim(char& out, CSVError& error) const {
        error.clear();
        if (this->possible_delimiters.size() != 1) {
            return internals::fail_static(error, CSVErrorCode::InvalidFormat,
                this->possible_delimiters.empty() ? "At least one delimiter is required."
                    : internals::ERROR_MULTIPLE_DELIMITERS);
        }
        out = this->possible_delimiters.front();
        return true;
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

#if CSV_EXCEPTIONS_ENABLED
    CSV_INLINE CSVFormat& CSVFormat::chunk_size(size_t size) {
        CSVError error;
        this->try_chunk_size(size, error);
        internals::throw_error(error);
        return *this;
    }
#endif

    CSV_INLINE bool CSVFormat::try_chunk_size(size_t size, CSVError& error) {
        error.clear();
        if (size < internals::CSV_CHUNK_SIZE_FLOOR) {
            return internals::fail(error, CSVErrorCode::InvalidChunkSize,
                internals::make_chunk_size_error(internals::CSV_CHUNK_SIZE_FLOOR, size));
        }
        const size_t max_chunk_size = internals::CSV_CHUNK_SIZE_MAX;
        if (size > max_chunk_size) {
            return internals::fail(error, CSVErrorCode::InvalidChunkSize,
                internals::make_chunk_size_ceiling_error(max_chunk_size, size));
        }
        this->_chunk_size = size;
        return true;
    }

    CSV_INLINE bool CSVFormat::validate_characters(
        const std::vector<char>& candidates, const std::vector<char>& whitespace,
        char quote, CSVError& error
    ) {
        error.clear();
        if (candidates.empty()) {
            return internals::fail_static(error, CSVErrorCode::InvalidFormat, "At least one delimiter is required.");
        }
        const std::set<char> delims(candidates.begin(), candidates.end());
        const std::set<char> trims(whitespace.begin(), whitespace.end());
        std::set<char> offenders;

        for (std::set<char>::const_iterator it = delims.begin(); it != delims.end(); ++it) {
            if (trims.find(*it) != trims.end()) offenders.insert(*it);
        }

        // Make sure quote character is not contained in possible delimiters
        // or whitespace characters.
        if (delims.find(quote) != delims.end() || trims.find(quote) != trims.end()) {
            offenders.insert(quote);
        }
        if (!offenders.empty()) {
            return internals::fail(error, CSVErrorCode::InvalidFormat, internals::make_char_overlap_error(offenders));
        }
        return true;
    }
    CSV_INLINE CSVFormat CSVFormat::guess_csv() {
        CSVFormat format;
        format.possible_delimiters = { ',', '|', '\t', ';', '^' };
        // Assign header directly rather than via header_row() so that
        // header_explicitly_set_ remains false — the guesser must be free
        // to detect the real header row at construction time.
        format.header = 0;

        return format;
    }

}
