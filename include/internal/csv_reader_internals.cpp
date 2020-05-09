#include "csv_reader_internals.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE BufferPtr parse(const ParseData& data) {
            using internals::ParseFlags;

            // Optimizations
            auto * HEDLEY_RESTRICT parse_flags = data.parse_flags.data();
            auto * HEDLEY_RESTRICT ws_flags = data.ws_flags.data();
            auto& in = data.in;
            auto& row_buffer = *(data.row_buffer.get());
            auto& text_buffer = row_buffer.buffer;
            auto& split_buffer = row_buffer.split_buffer;
            text_buffer.reserve(data.in.size());
            split_buffer.reserve(data.in.size() / 10);

            for (size_t i = 0; i < in.size(); i++) {
                switch (parse_flags[data.in[i] + 128]) {
                case ParseFlags::DELIMITER:
                    if (!data.quote_escape) {
                        split_buffer.push_back((internals::StrBufferPos)row_buffer.size());
                        break;
                    }

                    HEDLEY_FALL_THROUGH;
                case ParseFlags::NEWLINE:
                    if (!data.quote_escape) {
                        // End of record -> Write record
                        if (i + 1 < in.size() && in[i + 1] == '\n') // Catches CRLF (or LFLF)
                            ++i;

                        data.records.push_back(CSVRow(data.row_buffer));
                        break;
                    }

                    // Treat as regular character
                    text_buffer += in[i];
                    break;
                case ParseFlags::NOT_SPECIAL: {
                    size_t start, end;

                    if (!parse_not_special(
                        in,
                        parse_flags,
                        ws_flags,
                        i,
                        start,
                        end
                    )) {
                        break;
                    }

                    // Finally append text
#ifdef CSV_HAS_CXX17
                    text_buffer += in.substr(start, end - start + 1);
#else
                    for (; start < end + 1; start++) {
                        text_buffer += in[start];
                    }
#endif

                    break;
                }
                default: // Quote
                    if (!data.quote_escape) {
                        // Don't deref past beginning
                        if (i && parse_flags[in[i - 1] + 128] >= ParseFlags::DELIMITER) {
                            // Case: Previous character was delimiter or newline
                            data.quote_escape = true;
                        }

                        break;
                    }

                    auto next_ch = parse_flags[in[i + 1] + 128];
                    if (next_ch >= ParseFlags::DELIMITER) {
                        // Case: Delim or newline => end of field
                        data.quote_escape = false;
                        break;
                    }

                    // Case: Escaped quote
                    text_buffer += in[i];

                    // Note: Unescaped single quotes can be handled by the parser
                    if (next_ch == ParseFlags::QUOTE)
                        ++i;  // Case: Two consecutive quotes

                    break;
                }
            }

            return row_buffer.reset();
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename) {
            const size_t bytes = 500000;
            std::ifstream infile(filename.data());
            if (!infile.is_open()) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            std::unique_ptr<char[]> buffer(new char[bytes + 1]);
            char * head_buffer = buffer.get();

            for (size_t i = 0; i < bytes + 1; i++) {
                head_buffer[i] = '\0';
            }

            infile.read(head_buffer, bytes);
            return std::string(head_buffer);
        }
    }
}