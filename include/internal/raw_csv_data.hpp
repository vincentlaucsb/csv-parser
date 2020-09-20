#pragma once
#include <array>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "col_names.hpp"
#include "compatibility.hpp"
#include "csv_row.hpp"

namespace csv {
    namespace internals {
        /**  @typedef ParseFlags
         *   An enum used for describing the significance of each character
         *   with respect to CSV parsing
         */
        enum class ParseFlags {
            NOT_SPECIAL, /**< Characters with no special meaning */
            QUOTE,       /**< Characters which may signify a quote escape */
            DELIMITER,   /**< Characters which may signify a new field */
            NEWLINE      /**< Characters which may signify a new row */
        };

        using ParseFlagMap = std::array<ParseFlags, 256>;
        using WhitespaceMap = std::array<bool, 256>;
    }

    /** A class for parsing raw CSV data */
    class BasicCSVParser {
    public:
        BasicCSVParser() = default;
        BasicCSVParser(internals::ColNamesPtr _col_names) : col_names(_col_names) {};
        BasicCSVParser(internals::ParseFlagMap _parse_flags, internals::WhitespaceMap _ws_flags) :
            parse_flags(_parse_flags), ws_flags(_ws_flags) {};

        bool parse(csv::string_view in, std::deque<CSVRow>& records);
        void end_feed(std::deque<CSVRow>& records) {
            if (this->field_length > 0) {
                this->push_field();
            }

            if (this->current_row.size() > 0) {
                this->push_row(records);
            }
        }

        void set_parse_flags(internals::ParseFlagMap parse_flags) {
            this->parse_flags = parse_flags;
        }

        void set_ws_flags(internals::WhitespaceMap ws_flags) {
            this->ws_flags = ws_flags;
        }

    private:
        CONSTEXPR internals::ParseFlags parse_flag(const char ch) const {
            return this->parse_flags.data()[ch + 128];
        }

        CONSTEXPR bool ws_flag(const char ch) const {
            return this->ws_flags.data()[ch + 128];
        }

        void push_field();
        void push_row(std::deque<CSVRow>& records) {
            current_row.row_length = current_row.data->fields.size() - current_row.field_bounds_index;
            records.push_back(std::move(current_row));
        };

        void parse_loop(csv::string_view in,
        size_t start_offset = 0);

        void set_data_ptr(RawCSVDataPtr ptr) {
            this->data_ptr = ptr;
            this->fields = &(ptr->fields);
        }

        /** An array where the (i + 128)th slot gives the ParseFlags for ASCII character i */
        internals::ParseFlagMap parse_flags;

        /** An array where the (i + 128)th slot determines whether ASCII character i should
         *  be trimmed
         */
        internals::WhitespaceMap ws_flags;

        internals::ColNamesPtr col_names = nullptr;

        CSVRow current_row;
        bool quote_escape = false;
        size_t current_row_start = 0;
        int field_start = -1;
        unsigned int field_length = 0;
        bool field_has_double_quote = false;

        RawCSVDataPtr data_ptr = nullptr;
        std::vector<RawCSVField>* fields = nullptr;

        std::deque<CSVRow>* records = nullptr;
    };
}