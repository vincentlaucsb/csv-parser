#pragma once

#include "../parser/driver.hpp"

namespace csv {
    namespace internals {
        namespace speculative {
        struct CSVRowFragment {
            CSVRowFragment() = default;
            CSVRowFragment(
                csv::string_view bytes,
                std::shared_ptr<void> owner,
                ParserDFAState initial_state = ParserDFAState(),
                ParserDFAState ending_state = ParserDFAState(),
                size_t offset = 0
            ) : bytes(bytes),
                owner(std::move(owner)),
                initial_state(initial_state),
                ending_state(ending_state),
                offset(offset),
                present(true) {}

            bool empty() const noexcept {
                return !present;
            }

            static CSVRowFragment from_row(
                const CSVRow& row,
                ParserDFAState initial_state = ParserDFAState(),
                ParserDFAState ending_state = ParserDFAState(),
                size_t chunk_offset = 0
            ) {
                return CSVRowFragment(
                    row.raw_str(),
                    row.data,
                    initial_state,
                    ending_state,
                    chunk_offset + row.data_start
                );
            }

            csv::string_view bytes;
            std::shared_ptr<void> owner;
            ParserDFAState initial_state;
            ParserDFAState ending_state;
            size_t offset = 0;
            bool present = false;
        };

        inline CSVRowFragment concatenate_row_fragments(
            const CSVRowFragment& left,
            const CSVRowFragment& right
        ) {
            if (left.empty()) {
                return right;
            }

            if (right.empty()) {
                return left;
            }

            auto bytes = std::make_shared<std::string>();
            bytes->reserve(left.bytes.size() + right.bytes.size());
            if (!left.bytes.empty()) {
                bytes->append(left.bytes.data(), left.bytes.size());
            }
            if (!right.bytes.empty()) {
                bytes->append(right.bytes.data(), right.bytes.size());
            }

            return CSVRowFragment(
                csv::string_view(*bytes),
                bytes,
                left.initial_state,
                right.ending_state,
                left.offset
            );
        }

        struct ParsedChunkRows {
            size_t sequence_number = 0;
            size_t offset = 0;
            csv::string_view chunk;
            std::shared_ptr<void> owner;
            bool starts_at_record_boundary = true;
            bool scan_bom = true;
            ParserChunkResult parse_result;
            CSVRowFragment prefix_fragment;
            std::vector<CSVRow> complete_rows;
            CSVRowFragment suffix_fragment;
        };

        inline ParsedChunkRows split_parsed_chunk_rows(
            size_t sequence_number,
            csv::string_view chunk,
            std::shared_ptr<void> owner,
            const ParserChunkResult& parse_result,
            std::vector<CSVRow> parsed_rows,
            bool starts_at_record_boundary,
            size_t chunk_offset = 0
        ) {
            ParsedChunkRows result;
            result.sequence_number = sequence_number;
            result.offset = chunk_offset;
            result.chunk = chunk;
            result.owner = owner;
            result.starts_at_record_boundary = starts_at_record_boundary
                || parse_result.initial_state.pending_linefeed;
            result.scan_bom = starts_at_record_boundary && sequence_number == 0;
            result.parse_result = parse_result;

            size_t first_complete_row = 0;
            if (!result.starts_at_record_boundary) {
                if (parsed_rows.empty()) {
                    result.prefix_fragment = CSVRowFragment(
                        chunk,
                        owner,
                        parse_result.initial_state,
                        parse_result.ending_state,
                        chunk_offset
                    );
                    return result;
                }

                result.prefix_fragment = CSVRowFragment::from_row(
                    parsed_rows.front(),
                    parse_result.initial_state,
                    ParserDFAState(),
                    chunk_offset
                );
                first_complete_row = 1;
            }

            result.complete_rows.reserve(parsed_rows.size() - first_complete_row);
            for (size_t i = first_complete_row; i < parsed_rows.size(); ++i) {
                result.complete_rows.push_back(std::move(parsed_rows[i]));
            }

            if (parse_result.complete_prefix_length < chunk.size()) {
                result.suffix_fragment = CSVRowFragment(
                    chunk.substr(parse_result.complete_prefix_length),
                    owner,
                    ParserDFAState(),
                    parse_result.ending_state,
                    chunk_offset + parse_result.complete_prefix_length
                );
            }

            return result;
        }

        template<typename Parser>
        inline std::vector<CSVRow> materialize_row_fragment(
            Parser& parser,
            const CSVRowFragment& fragment
        ) {
            std::vector<CSVRow> rows;
            if (fragment.empty()) {
                return rows;
            }

            parser.parse_chunk(
                fragment.bytes,
                fragment.owner,
                rows,
                ParserChunkOptions(ParserDFAState(), false, fragment.offset)
            );
            parser.end_feed();
            return rows;
        }

        template<typename Parser>
        inline ParsedChunkRows repair_parsed_chunk_rows(
            Parser& parser,
            const ParsedChunkRows& chunk,
            ParserDFAState corrected_initial_state
        ) {
            std::vector<CSVRow> parsed_rows;

            const ParserChunkResult parse_result = parser.parse_chunk(
                chunk.chunk,
                chunk.owner,
                parsed_rows,
                ParserChunkOptions(corrected_initial_state, chunk.scan_bom, chunk.offset)
            );

            return split_parsed_chunk_rows(
                chunk.sequence_number,
                chunk.chunk,
                chunk.owner,
                parse_result,
                std::move(parsed_rows),
                chunk.starts_at_record_boundary,
                chunk.offset
            );
        }

        /** Minimal parser shell for caller-owned chunks.
         *
         *  The SIGMOD-style speculative path treats input sourcing as an
         *  external concern. This parser core only needs delimiter/whitespace state.
         */
        template<bool EagerClassify = false>
        class ChunkParserCoreT : public CSVParserCore<
            std::vector<CSVRow>,
            PermissiveParsePolicy,
            CSVRowFieldPolicy<EagerClassify>,
            CSVRowRowPolicy> {
            using Base = CSVParserCore<
                std::vector<CSVRow>,
                PermissiveParsePolicy,
                CSVRowFieldPolicy<EagerClassify>,
                CSVRowRowPolicy>;

        public:
            ChunkParserCoreT(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags
            ) : Base(parse_flags, ws_flags) {}

            ChunkParserCoreT(
                const ParseFlagMap& parse_flags,
                const WhitespaceMap& ws_flags,
                const ColNamesPtr& col_names
            ) : Base(parse_flags, ws_flags, col_names) {}
        };

        using ChunkParserCore = ChunkParserCoreT<false>;
        }
    }
}
