/** @file
 *  @brief Shared exception message templates and throw helpers.
 */

#pragma once

#include <set>
#include <stdexcept>
#include <string>
#include <system_error>

#include "common.hpp"

namespace csv {
    namespace internals {
        CONSTEXPR_VALUE_14 char ERROR_CANNOT_OPEN_FILE[] = "Cannot open file ";
        CONSTEXPR_VALUE_14 char ERROR_FAILED_OPEN_WRITE[] = "Failed to open file for writing: ";
        CONSTEXPR_VALUE_14 char ERROR_STREAM_READ_FAILURE[] = "StreamParser read failure";
        CONSTEXPR_VALUE_14 char ERROR_UNSUPPORTED_ENCODING_SUFFIX[] =
            " encoded CSV input is not supported directly. Please transcode to UTF-8 before parsing.";
        CONSTEXPR_VALUE_14 char ERROR_ROW_TOO_SHORT[] = "Line too short ";
        CONSTEXPR_VALUE_14 char ERROR_ROW_TOO_LONG[] = "Line too long ";
        CONSTEXPR_VALUE_14 char ERROR_ROW_LARGER_THAN_CHUNK_PREFIX[] =
            "End of file not reached and no more records parsed. "
            "This likely indicates a CSV row larger than the chunk size of ";
        CONSTEXPR_VALUE_14 char ERROR_ROW_LARGER_THAN_CHUNK_SUFFIX[] =
            " bytes. Use CSVFormat::chunk_size() to increase the chunk size.";
        CONSTEXPR_VALUE_14 char ERROR_COLUMN_NOT_FOUND[] = "Column not found: ";
        CONSTEXPR_VALUE_14 char ERROR_COLUMN_INDEX_OUT_OF_BOUNDS[] = "Column index out of bounds.";
        CONSTEXPR_VALUE_14 char ERROR_COLUMN_INDEX_OUT_OF_RANGE[] = "Column index out of range.";
        CONSTEXPR_VALUE_14 char CSV_ERROR_INDEX_OUT_OF_BOUNDS[] = "Index out of bounds.";
        CONSTEXPR_VALUE_14 char ERROR_CANNOT_EDIT_CONST_DF_CELL[] = "Cannot edit a const DataFrame cell.";
        CONSTEXPR_VALUE_14 char ERROR_CANNOT_ERASE_CONST_DF_ROW[] = "Cannot erase a const DataFrame row.";
        CONSTEXPR_VALUE_14 char ERROR_COLUMN_APPLY_STATE_COUNT[] =
            "column_parallel_apply() requires one state object per column.";
        CONSTEXPR_VALUE_14 char ERROR_COLUMN_APPLY_SUBSET_STATE_COUNT[] =
            "column_parallel_apply() subset overload requires one state object per selected column.";
        CONSTEXPR_VALUE_14 char ERROR_COLUMN_APPLY_INVALID_INDEX[] =
            "column_parallel_apply() subset overload received an invalid column index.";
        CONSTEXPR_VALUE_14 char ERROR_KEY_COLUMN_EMPTY[] = "Key column cannot be empty.";
        CONSTEXPR_VALUE_14 char ERROR_KEY_COLUMN_NOT_FOUND[] = "Key column not found: ";
        CONSTEXPR_VALUE_14 char ERROR_KEY_COLUMN_VALUE[] = "Error retrieving key column value: ";
        CONSTEXPR_VALUE_14 char ERROR_DUPLICATE_KEY[] = "Duplicate key encountered.";
        CONSTEXPR_VALUE_14 char ERROR_UNKEYED_DATA_FRAME[] =
            "This DataFrame was created without a key column.";
        CONSTEXPR_VALUE_14 char ERROR_KEY_NOT_FOUND[] = "Key not found.";
        CONSTEXPR_VALUE_14 char ERROR_CHUNK_PARALLEL_APPLY_ZERO[] =
            "chunk_parallel_apply() requires a non-zero chunk size.";
        CONSTEXPR_VALUE_14 char ERROR_READER_NULL_STREAM[] = "CSVReader requires a non-null stream";
        CONSTEXPR_VALUE_14 char ERROR_MULTIPLE_DELIMITERS[] =
            "There is more than one possible delimiter.";
        CONSTEXPR_VALUE_14 char ERROR_CHUNK_SIZE_FLOOR_PREFIX[] = "Chunk size must be at least ";
        CONSTEXPR_VALUE_14 char ERROR_CHUNK_SIZE_FLOOR_MIDDLE[] = " bytes (500KB). Provided: ";
        CONSTEXPR_VALUE_14 char ERROR_CHUNK_SIZE_CEILING_PREFIX[] = "Chunk size must fit in uint32_t. Maximum: ";
        CONSTEXPR_VALUE_14 char ERROR_CHUNK_SIZE_CEILING_MIDDLE[] = ". Provided: ";
        CONSTEXPR_VALUE_14 char ERROR_CHAR_OVERLAP_PREFIX[] =
            "There should be no overlap between the quote character, "
            "the set of possible delimiters "
            "and the set of whitespace characters. Offending characters: ";

        inline std::string make_prefixed_message(const char* prefix, csv::string_view value) {
            return std::string(prefix) + std::string(value);
        }

        inline std::string make_unsupported_encoding_message(const char* encoding) {
            return std::string(encoding) + ERROR_UNSUPPORTED_ENCODING_SUFFIX;
        }

        inline std::string make_chunk_size_error(size_t floor, size_t provided) {
            return std::string(ERROR_CHUNK_SIZE_FLOOR_PREFIX)
                + std::to_string(floor)
                + ERROR_CHUNK_SIZE_FLOOR_MIDDLE
                + std::to_string(provided);
        }

        inline std::string make_chunk_size_ceiling_error(size_t ceiling, size_t provided) {
            return std::string(ERROR_CHUNK_SIZE_CEILING_PREFIX)
                + std::to_string(ceiling)
                + ERROR_CHUNK_SIZE_CEILING_MIDDLE
                + std::to_string(provided);
        }

        inline std::string make_row_larger_than_chunk_message(size_t chunk_size) {
            return std::string(ERROR_ROW_LARGER_THAN_CHUNK_PREFIX)
                + std::to_string(chunk_size)
                + ERROR_ROW_LARGER_THAN_CHUNK_SUFFIX;
        }

        inline std::string make_mmap_failure_message(
            const std::string& filename,
            size_t offset,
            size_t length
        ) {
            return "Memory mapping failed during CSV parsing: file='" + filename
                + "' offset=" + std::to_string(offset)
                + " length=" + std::to_string(length);
        }

        inline std::string make_char_overlap_error(const std::set<char>& offenders) {
            std::string err_msg = ERROR_CHAR_OVERLAP_PREFIX;

            size_t i = 0;
            for (std::set<char>::const_iterator it = offenders.begin(); it != offenders.end(); ++it, ++i) {
                err_msg += "'";
                err_msg += *it;
                err_msg += "'";

                if (i + 1 < offenders.size()) {
                    err_msg += ", ";
                }
            }

            err_msg += '.';
            return err_msg;
        }

        [[noreturn]] inline void throw_cannot_open_file(csv::string_view filename) {
            throw std::runtime_error(make_prefixed_message(ERROR_CANNOT_OPEN_FILE, filename));
        }

        [[noreturn]] inline void throw_failed_open_for_writing(const std::string& filename) {
            throw std::runtime_error(std::string(ERROR_FAILED_OPEN_WRITE) + filename);
        }

        [[noreturn]] inline void throw_unsupported_encoding(const char* encoding) {
            throw std::runtime_error(make_unsupported_encoding_message(encoding));
        }

        [[noreturn]] inline void throw_stream_read_failure() {
            throw std::runtime_error(ERROR_STREAM_READ_FAILURE);
        }

        [[noreturn]] inline void throw_mmap_failure(
            const std::error_code& error,
            const std::string& filename,
            size_t offset,
            size_t length
        ) {
            throw std::system_error(error, make_mmap_failure_message(filename, offset, length));
        }

        [[noreturn]] inline void throw_row_too_large_for_chunk(size_t chunk_size) {
            throw std::runtime_error(make_row_larger_than_chunk_message(chunk_size));
        }

        [[noreturn]] inline void throw_line_too_short(csv::string_view raw_row) {
            throw std::runtime_error(make_prefixed_message(ERROR_ROW_TOO_SHORT, raw_row));
        }

        [[noreturn]] inline void throw_line_too_long(csv::string_view raw_row) {
            throw std::runtime_error(make_prefixed_message(ERROR_ROW_TOO_LONG, raw_row));
        }

        [[noreturn]] inline void throw_column_not_found(csv::string_view column) {
            throw std::runtime_error(make_prefixed_message(ERROR_COLUMN_NOT_FOUND, column));
        }

        [[noreturn]] inline void throw_column_not_found_out_of_range(csv::string_view column) {
            throw std::out_of_range(make_prefixed_message(ERROR_COLUMN_NOT_FOUND, column));
        }

        [[noreturn]] inline void throw_column_index_out_of_bounds() {
            throw std::out_of_range(ERROR_COLUMN_INDEX_OUT_OF_BOUNDS);
        }

        [[noreturn]] inline void throw_column_index_out_of_range() {
            throw std::out_of_range(ERROR_COLUMN_INDEX_OUT_OF_RANGE);
        }
    }
}
