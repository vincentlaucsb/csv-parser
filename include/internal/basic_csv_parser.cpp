#include "basic_csv_parser.hpp"

#include <system_error>

namespace csv {
    namespace internals {
#if defined(__EMSCRIPTEN__)
        // Opens the file and delegates to the template overload to avoid duplicating the read/resize logic.
        CSV_INLINE std::string get_csv_head_stream(csv::string_view filename) {
            std::ifstream infile(std::string(filename), std::ios::binary);
            if (!infile.is_open()) {
                throw_cannot_open_file(filename);
            }
            return get_csv_head_stream(infile);
        }
#endif

#if !defined(__EMSCRIPTEN__)
        CSV_INLINE std::pair<std::string, size_t> get_csv_head_mmap(csv::string_view filename) {
            const size_t bytes = 500000;
            std::error_code error;
            auto mmap = mio::make_mmap_source(std::string(filename), 0, mio::map_entire_file, error);
            if (error) {
                throw_cannot_open_file(filename);
            }
            const size_t file_size = mmap.size();
            const size_t length = std::min(file_size, bytes);
            return { std::string(mmap.begin(), mmap.begin() + length), file_size };
        }
#endif

        CSV_INLINE std::string get_csv_head(csv::string_view filename) {
#if defined(__EMSCRIPTEN__)
            return get_csv_head_stream(filename);
#else
            return get_csv_head_mmap(filename).first;
#endif
        }

        CSV_INLINE size_t get_bom_skip_or_throw(csv::string_view data, bool& utf8_bom) {
            utf8_bom = false;

            if (data.size() >= 4) {
                const unsigned char b0 = static_cast<unsigned char>(data[0]);
                const unsigned char b1 = static_cast<unsigned char>(data[1]);
                const unsigned char b2 = static_cast<unsigned char>(data[2]);
                const unsigned char b3 = static_cast<unsigned char>(data[3]);

                if ((b0 == 0xFF && b1 == 0xFE && b2 == 0x00 && b3 == 0x00)
                    || (b0 == 0x00 && b1 == 0x00 && b2 == 0xFE && b3 == 0xFF)) {
                    throw_unsupported_encoding("UTF-32");
                }
            }

            if (data.size() >= 3) {
                const unsigned char b0 = static_cast<unsigned char>(data[0]);
                const unsigned char b1 = static_cast<unsigned char>(data[1]);
                const unsigned char b2 = static_cast<unsigned char>(data[2]);

                if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {
                    utf8_bom = true;
                    return 3;
                }
            }

            if (data.size() >= 2) {
                const unsigned char b0 = static_cast<unsigned char>(data[0]);
                const unsigned char b1 = static_cast<unsigned char>(data[1]);

                if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
                    throw_unsupported_encoding("UTF-16");
                }
            }

            return 0;
        }


#ifdef _MSC_VER
#pragma region CSVParserDriverBase
#endif

        CSV_INLINE CSVParserDriverBase::CSVParserDriverBase(
            const CSVFormat& source_format,
            const ColNamesPtr& col_names
        ) : CSVParserCore<>(source_format, col_names) {}

        CSV_INLINE void CSVParserDriverBase::resolve_format_from_head(const CSVFormat& source_format) {
            auto head = this->get_csv_head();

            ResolvedFormat resolved;
            resolved.format = source_format;

            const bool infer_delimiter = source_format.guess_delim();
            const bool infer_header = !source_format.header_explicitly_set_
                && (infer_delimiter || !source_format.col_names_explicitly_set_);
            const bool infer_n_cols = (source_format.get_header() < 0 && source_format.get_col_names().empty());

            if (infer_delimiter || infer_header || infer_n_cols) {
                auto guess_result = guess_format(head, source_format.get_possible_delims());
                if (infer_delimiter) {
                    resolved.format.delimiter(guess_result.delim);
                }

                if (infer_header) {
                    // Inferred header should not clear user-provided column names.
                    resolved.format.header = guess_result.header_row;
                }

                resolved.n_cols = guess_result.n_cols;
            }

            if (resolved.format.no_quote) {
                this->set_parse_flags(
                    internals::make_parse_flags(resolved.format.get_delim()),
                    resolved.format.get_delim(),
                    resolved.format.get_delim()
                );
            }
            else {
                this->set_parse_flags(
                    internals::make_parse_flags(resolved.format.get_delim(), resolved.format.quote_char),
                    resolved.format.get_delim(),
                    resolved.format.quote_char
                );
            }

            this->format = resolved;
        }
#ifdef _MSC_VER
#pragma endregion
#endif
    }
}
