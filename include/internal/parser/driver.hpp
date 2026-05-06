/** @file
 *  @brief Contains CSV parser source-adapter declarations and format helpers.
 */

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#include "../../external/mio.hpp"
#endif
#include "../basic_csv_parser_simd.hpp"
#include "../col_names.hpp"
#include "../common.hpp"
#include "../csv_exceptions.hpp"
#include "../csv_format.hpp"
#include "../csv_parser_core.hpp"
#include "../csv_row.hpp"
#include "../speculative/diagnostics.hpp"

namespace csv {
    namespace internals {
        namespace parser {
        struct GuessScore {
            size_t header;
            size_t mode_row_length;
            double score;
        };

        CSV_INLINE GuessScore calculate_score(csv::string_view head, const CSVFormat& format);

        /** Guess the delimiter used by a delimiter-separated values file. */
        CSV_INLINE CSVGuessResult guess_format(
            csv::string_view head,
            const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' }
        );

        /** Read the first 500KB from a seekless stream source. */
        template<typename TStream,
            csv::enable_if_t<std::is_base_of<std::istream, TStream>::value, int>  = 0>
        std::string get_csv_head_stream(TStream& source) {
            const size_t limit = 500000;
            std::string buf(limit, '\0');
            source.read(&buf[0], (std::streamsize)limit);
            buf.resize(static_cast<size_t>(source.gcount()));
            return buf;
        }

#if defined(__EMSCRIPTEN__)
        /** Open a file-backed source and read the first 500KB through the stream path. */
        CSV_INLINE std::string get_csv_head_stream(csv::string_view filename);
#endif

#if !defined(__EMSCRIPTEN__)
        /** Read the first 500KB from a filename using mmap.
         *  Also returns the total file size so callers avoid a second mmap open.
         */
        CSV_INLINE std::pair<std::string, size_t> get_csv_head_mmap(csv::string_view filename);
#endif

        /** Compatibility shim selecting stream on Emscripten and mmap otherwise. */
        CSV_INLINE std::string get_csv_head(csv::string_view filename);

        struct ResolvedFormat {
            CSVFormat format;
            size_t n_cols = 0;
        };

        class CSVParserDriverBase;
        }
    }

    /** @brief Guess the delimiter, header row, and mode column count of a CSV file
         *
         *  **Heuristic:** For each candidate delimiter, calculate a score based on
         *  the most common row length (mode). The delimiter with the highest score wins.
         *
         *  **Header Detection:**
         *  - If the first row has >= columns than the mode, it's treated as the header
         *  - Otherwise, the first row with the mode length is treated as the header
         *
         *  This approach handles:
         *  - Headers with trailing delimiters or optional columns (wider than data rows)
         *  - Comment lines before the actual header (first row shorter than mode)
         *  - Standard CSVs where first row is the header
         *
        *  @note Score = (row_length * count_of_rows_with_that_length)
        *  @note Also returns inferred mode-width column count (CSVGuessResult::n_cols)
         */
    inline CSVGuessResult guess_format(csv::string_view filename,
        const std::vector<char>& delims = { ',', '|', '\t', ';', '^', '~' }) {
        auto head = internals::parser::get_csv_head(filename);
        return internals::parser::guess_format(head, delims);
    }

    namespace internals {
        namespace parser {

        class ICSVParseOrchestrator {
        public:
            virtual ~ICSVParseOrchestrator() {}

            virtual size_t worker_count() const noexcept = 0;
            virtual SpeculativeParseDiagnostics diagnostics() const noexcept = 0;
            virtual size_t read_window_size(size_t chunk_size) const noexcept = 0;
            virtual bool utf8_bom() const noexcept = 0;
            virtual void reset_with_initial_state(ParserDFAState state) noexcept = 0;
            virtual ParserDFAState ending_state() const noexcept = 0;

            virtual CSVParseWindowResult parse_window(
                csv::string_view chunk,
                std::shared_ptr<void> owner,
                size_t base_offset,
                size_t serial_chunk_size,
                bool source_exhausted,
                RowCollection& output
            ) = 0;
        };

        inline std::unique_ptr<ICSVParseOrchestrator> make_csv_parse_orchestrator(
            const ParseFlagMap& parse_flags,
            const WhitespaceMap& ws_flags,
            const CSVFormat& format,
            size_t source_size,
            const ColNamesPtr& col_names = nullptr,
            bool enable_speculative_parallel = true,
            bool source_size_known = true
        );

        /** Abstract base class for source adapters.
         *
         *  It preserves the existing parser API while delegating byte-level
         *  parsing to CSVParserCore.
         */
        class CSVParserDriverBase : public CSVParserCore<> {
        public:
            CSVParserDriverBase() = default;
            CSVParserDriverBase(const CSVFormat&, const ColNamesPtr&);
            CSVParserDriverBase(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags
            ) : CSVParserCore<>(parse_flags, ws_flags) {}

            virtual ~CSVParserDriverBase() {}

            /** Whether or not we have reached the end of source */
            bool eof() { return this->eof_; }

            ResolvedFormat get_resolved_format() { return this->format; }

            /** Parse the next block of data */
            virtual void next(size_t bytes) = 0;

            virtual SpeculativeParseDiagnostics speculative_diagnostics() const noexcept {
                return SpeculativeParseDiagnostics();
            }

            virtual size_t parse_worker_count() const noexcept {
                return 1;
            }

            virtual bool utf8_bom() const noexcept {
                return this->parse_orchestrator_
                    ? this->parse_orchestrator_->utf8_bom()
                    : CSVParserCore<>::utf8_bom();
            }

        protected:
            /** @name Current Stream/File State */
            ///@{
            bool eof_ = false;

            ResolvedFormat format;

            /** The size of the incoming CSV */
            size_t source_size_ = 0;
            ///@}

            std::unique_ptr<ICSVParseOrchestrator> parse_orchestrator_;

            virtual std::string& get_csv_head() = 0;

            void resolve_format_from_head(const CSVFormat& format);
        };
        }
    }
}
