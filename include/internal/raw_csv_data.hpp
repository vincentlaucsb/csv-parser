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

    /**
    union RawCSVString {
        RawCSVString() { this->csv_string = ""; }

        internals::WorkItem csv_string_ptr;
        std::string csv_string;
    };
    **/

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
        // NOTE/TODO: Fill out this method first
        // It'll give you clues on the rest
        
        bool parse(csv::string_view in, internals::ParseFlagMap _parse_flags, internals::WhitespaceMap _ws_flags, std::deque<RawCSVRow>& records);

        RawCSVField parse_quoted_field(csv::string_view in, internals::ParseFlags parse_flags[], size_t row_start, size_t& i, bool& quote_escape);

        size_t stitch(csv::string_view in, internals::ParseFlags parse_flags[], bool WhitespaceFlags[], std::deque<RawCSVRow>& records);

    private:
        struct ParseLoopData {
            csv::string_view in;
            internals::ParseFlagMap parse_flags;
            internals::WhitespaceMap ws_flags;
            csv::RawCSVDataPtr raw_data;
            std::deque<RawCSVRow> * records;
            bool is_stitching = false;
            size_t start_offset = 0;
        };

        RawCSVField parse_field(csv::string_view in, internals::ParseFlags parse_flags[], size_t row_start, size_t& i);

        size_t parse_loop(ParseLoopData& data);

        RawCSVRow current_row;
        bool quote_escape = false;
    };
}