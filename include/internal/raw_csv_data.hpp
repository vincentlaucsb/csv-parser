/** @file
 *  @brief Internal data structures for CSV parsing
 * 
 *  This file contains the low-level structures used by the parser to store
 *  CSV data before it's exposed through the public CSVRow/CSVField API.
 * 
 *  Data flow: Parser -> RawCSVData -> CSVRow -> CSVField
 */

#pragma once

#include <memory>

#include "col_names.hpp"
#include "common.hpp"
#include "memory/block_arena.hpp"
#include "memory/constants.hpp"
#include "memory/quote_arena.hpp"
#include "memory/raw_csv_field.hpp"
#include "memory/raw_csv_field_list.hpp"

namespace csv {
    namespace internals {
        class JsonConverter;

        using memory::INVALID_REALIZED_OFFSET;
        using memory::RawCSVBlockArena;
        using memory::RawCSVField;
        using memory::RawCSVFieldList;
        using memory::RawCSVQuoteArena;

        /** A class for storing raw CSV data and associated metadata
         * 
         *  This structure is the bridge between the parser thread and the main thread.
         *  Parser populates fields, data, and parse_flags; main thread reads via CSVRow.
         */
        struct RawCSVData {
            std::shared_ptr<void> _data = nullptr;
            csv::string_view data = "";

            internals::RawCSVFieldList fields;

            /** Parser-time sidecar bytes for fields whose quoted contents contained doubled quotes. */
            internals::RawCSVQuoteArena quote_arena;

            /** Cached JSON converter for rows sharing this parsed backing storage. */
            mutable internals::lazy_shared_ptr<JsonConverter> json_converter;

            internals::ColNamesPtr col_names = nullptr;
            internals::ParseFlagMap parse_flags;
            internals::WhitespaceMap ws_flags;

            /** True when at least one whitespace trim character is configured.
             *  Used by get_field_impl() to skip trim work in the common no-trim case.
             */
            bool has_ws_trimming = false;
        };

        using RawCSVDataPtr = std::shared_ptr<RawCSVData>;
    }
}
