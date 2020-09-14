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
        internals::WorkItem data_ptr;
        std::vector<RawCSVField> fields = {};
        csv::string_view get_string() {
            return csv::string_view(
                data_ptr.first.get(),
                data_ptr.second
            );
        }
    };

    using RawCSVDataPtr = std::shared_ptr<RawCSVData>;

    struct RawCSVRow {
        RawCSVRow() = default;
        RawCSVRow(RawCSVDataPtr _data) : data(_data) {}

        std::string get_field(size_t index);

        RawCSVDataPtr data;
        size_t data_start = 0;
        size_t row_length = 0;
    };

    /** A class for parsing raw CSV data */
    class BasicCSVParser {
    public:
        // NOTE/TODO: Fill out this method first
        // It'll give you clues on the rest
        bool parse(
            internals::WorkItem&& data,
            internals::ParseFlagMap parse_flags,
            internals::WhitespaceMap ws_flags,
            std::deque<RawCSVRow>& records
        );

        size_t stitch(csv::string_view in, internals::ParseFlags parse_flags[], bool WhitespaceFlags[], std::deque<RawCSVRow>& records);

        RawCSVField parse_quoted_field(csv::string_view in, internals::ParseFlags parse_flags[], size_t& i, bool& quote_escape);
        RawCSVField parse_field(csv::string_view in, internals::ParseFlags parse_flags[], size_t& i);

    private:
        RawCSVRow current_row;
        bool quote_escape = false;
    };
}