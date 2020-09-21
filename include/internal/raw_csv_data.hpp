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
        BasicCSVParser(internals::ParseFlagMap parse_flags, internals::WhitespaceMap ws_flags) :
            _parse_flags(parse_flags), _ws_flags(ws_flags) {};

        void parse(csv::string_view in, std::deque<CSVRow>& records);
        void end_feed(std::deque<CSVRow>& records) {
            using internals::ParseFlags;

            bool empty_last_field = !this->current_row.data->data.empty()
                && parse_flag(this->current_row.data->data.back()) == ParseFlags::DELIMITER;

            if (this->field_length > 0 || empty_last_field) {
                this->push_field();
            }

            if (this->current_row.size() > 0) {
                this->push_row(records);
            }
        }

        void set_parse_flags(internals::ParseFlagMap parse_flags) {
            _parse_flags = parse_flags;
        }

        void set_ws_flags(internals::WhitespaceMap ws_flags) {
            _ws_flags = ws_flags;
        }

    private:
        CONSTEXPR internals::ParseFlags parse_flag(const char ch) const {
            return _parse_flags.data()[ch + 128];
        }

        CONSTEXPR bool ws_flag(const char ch) const {
            return _ws_flags.data()[ch + 128];
        }

        void push_field();
        CONSTEXPR void parse_field(csv::string_view in, size_t& i, const size_t& current_row_start, bool quote_escape = false);

        void parse_loop(csv::string_view in);

        void push_row(std::deque<CSVRow>& records) {
            current_row.row_length = current_row.data->fields.size() - current_row.field_bounds_index;
            records.push_back(std::move(current_row));
        };

        void set_data_ptr(RawCSVDataPtr ptr) {
            this->data_ptr = ptr;
            this->fields = &(ptr->fields);
        }

        /** An array where the (i + 128)th slot gives the ParseFlags for ASCII character i */
        internals::ParseFlagMap _parse_flags;

        /** An array where the (i + 128)th slot determines whether ASCII character i should
         *  be trimmed
         */
        internals::WhitespaceMap _ws_flags;

        internals::ColNamesPtr col_names = nullptr;

        CSVRow current_row;
        int field_start = -1;
        size_t field_length = 0;
        bool field_has_double_quote = false;

        RawCSVDataPtr data_ptr = nullptr;
        internals::CSVFieldArray* fields = nullptr;
        
        std::deque<CSVRow>* _records = nullptr;
    };
}