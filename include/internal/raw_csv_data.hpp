#include <memory>
#include <vector>

#include "compatibility.hpp"
#include "csv_reader_internals.hpp"

namespace csv {
    struct RawCSVField {
        size_t start;
        size_t length;
        bool has_doubled_quote;
    };

    /** A class for storing raw CSV data and associated metadata */
    struct RawCSVData {
        std::string data = "";
        std::vector<RawCSVField> fields = {};
    };

    using RawCSVDataPtr = std::shared_ptr<RawCSVData>;

    struct RawCSVRow {
        RawCSVRow() = default;
        RawCSVRow(RawCSVDataPtr _data) : data(_data) {}

        std::string get_field(size_t index);
        std::string get_raw_data();
        size_t get_raw_data_length();
        std::vector<RawCSVField> get_raw_fields();
        size_t size() { return row_length;  }

        RawCSVDataPtr data;

        /** Where in RawCSVData.data we start */
        size_t data_start = 0;

        /** Where in the RawCSVDataPtr.fields array we start */
        size_t field_bounds_index = 0;

        /** How many columns this row spans */
        size_t row_length = 0;
    };

    /** A class for parsing raw CSV data */
    class BasicCSVParser {
    public:
        BasicCSVParser(internals::ParseFlagMap _parse_flags, internals::WhitespaceMap _ws_flags) :
            parse_flags(_parse_flags), ws_flags(_ws_flags) {};

        bool parse(csv::string_view in, std::deque<RawCSVRow>& records);

    private:
        struct ParseLoopData {
            csv::string_view in;
            csv::RawCSVDataPtr raw_data;
            std::deque<RawCSVRow> * records;
            bool is_stitching = false;
            size_t start_offset = 0;
        };

        void parse_field(csv::string_view in, internals::ParseFlags parse_flags[], size_t& i);
        void parse_quoted_field(csv::string_view in, internals::ParseFlags parse_flags[], size_t& i);
        void push_field();

        size_t parse_loop(ParseLoopData& data);

        RawCSVRow current_row;
        bool quote_escape = false;
        size_t current_row_start = 0;
        size_t field_start = 0;
        size_t field_length = 0;
        bool field_has_double_quote = false;

        internals::ParseFlagMap parse_flags;
        internals::WhitespaceMap ws_flags;
    };
}