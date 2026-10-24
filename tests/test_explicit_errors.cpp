#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/file_guard.hpp"
#include "shared/generated_file.hpp"
#include <fstream>
#include <sstream>
#include <streambuf>
#include <vector>

using namespace csv;

static_assert(CSV_EXCEPTIONS_ENABLED == CSV_TEST_EXCEPTIONS,
    "These tests must compile in the requested exception mode.");

namespace {
    const size_t large_row_count = 500000;

    const std::string& large_csv(bool malformed, bool early_short = false) {
        static csv_test::GeneratedFile valid("tests/data/fake_data/explicit_errors_large.csv");
        static csv_test::GeneratedFile invalid("tests/data/fake_data/explicit_errors_late_bad.csv");
        static csv_test::GeneratedFile short_row("tests/data/fake_data/explicit_errors_early_short.csv");
        return (early_short ? short_row : malformed ? invalid : valid).path(
            [malformed, early_short](std::ostream& out) {
            out << "a,b,c,d,e\n";
            for (size_t i = 0; i < large_row_count; ++i) {
                const size_t fields = early_short && i == 100 ? 4 : 5;
                for (size_t j = 0; j < fields; ++j) {
                    if (j) out << ',';
                    out << i * 5 + j;
                }
                out << '\n';
            }
            if (malformed) out << "1,2,3,4,5,6\n";
        });
    }

    CSVFormat large_format(bool threaded) {
        CSVFormat format;
        format.header_row(0).variable_columns(VariableColumnPolicy::THROW).threading(threaded);
        return format;
    }

    // Hide read() on a derived stream so the source adapter encounters badbit
    // after a full 10MB window, without throwing from test infrastructure.
    class LateFailStream : public std::ifstream {
    public:
        explicit LateFailStream(const std::string& filename)
            : std::ifstream(filename, std::ios::binary) {}
        LateFailStream& read(char* destination, std::streamsize size) {
            if (bytes_read_ >= internals::CSV_CHUNK_SIZE_DEFAULT) {
                this->setstate(std::ios::badbit);
                return *this;
            }
            const size_t remaining = internals::CSV_CHUNK_SIZE_DEFAULT - bytes_read_;
            const auto limited = std::min(size, static_cast<std::streamsize>(remaining));
            std::ifstream::read(destination, limited);
            bytes_read_ += static_cast<size_t>(this->gcount());
            return *this;
        }
    private:
        size_t bytes_read_ = 0;
    };

    class FailingWriteBuffer : public std::streambuf {
        std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
        int_type overflow(int_type) override { return traits_type::eof(); }
    };

    class FailingFlushBuffer : public std::stringbuf {
        int sync() override { return -1; }
    };

#if CSV_EXCEPTIONS_ENABLED
    class FailingReadBuffer : public std::streambuf {
        std::streamsize xsgetn(char*, std::streamsize) override {
            throw std::ios_base::failure("read failed");
        }
    };
#endif

    void require_closed_writer(CSVWriter<std::ofstream>& writer) {
        CSVError error;
        REQUIRE_FALSE(writer.try_flush(error));
        REQUIRE(error.code == CSVErrorCode::InvalidArgument);
        REQUIRE_FALSE(writer.try_write_row(error, "closed", 1));
        REQUIRE(error.code == CSVErrorCode::InvalidArgument);
    }
}

TEST_CASE("Explicit conversion errors share the expected error model", "[explicit_errors]") {
    CSVConversionError error = CSVConversionError::Overflow;
    int value = 42;
    REQUIRE(CSVField("17").try_get(value, error));
    REQUIRE(value == 17);
    REQUIRE(error == CSVConversionError::None);
    REQUIRE_FALSE(CSVField("text").try_get(value, error));
    REQUIRE(error == CSVConversionError::NotANumber);
    REQUIRE(value == 17);
    REQUIRE_FALSE(CSVField("1.5").try_get(value, error));
    REQUIRE(error == CSVConversionError::FloatToInt);
    unsigned positive = 12;
    REQUIRE_FALSE(CSVField("-1").try_get(positive, error));
    REQUIRE(error == CSVConversionError::NegativeToUnsigned);
    signed char small = 0;
    REQUIRE_FALSE(CSVField("128").try_get(small, error));
    REQUIRE(error == CSVConversionError::Overflow);
    std::string text;
    REQUIRE(CSVField("hello").try_get(text, error));
    REQUIRE(text == "hello");
    REQUIRE(error == CSVConversionError::None);
    csv::string_view view;
    REQUIRE(CSVField("view").try_get(view, error));
    REQUIRE(view == "view");
    long double number = 0;
    REQUIRE(CSVField("2.5").try_get(number, error));
    REQUIRE(number == 2.5L);
#ifdef CSV_HAS_STD_EXPECTED
    auto converted = CSVField("1.5").as<int>();
    REQUIRE_FALSE(converted);
    REQUIRE(converted.error() == CSVConversionError::FloatToInt);
#endif
#if CSV_EXCEPTIONS_ENABLED
    REQUIRE_THROWS_AS(CSVField("text").get<int>(), std::runtime_error);
#endif
}

TEST_CASE("Explicit format validation preserves the last valid configuration", "[explicit_errors]") {
    CSVFormat format;
    CSVError error;
    REQUIRE(format.try_delimiter('|', error));
    REQUIRE_FALSE(error);
    REQUIRE_FALSE(format.try_trim(std::vector<char>{'|'}, error));
    REQUIRE(error.code == CSVErrorCode::InvalidFormat);
    REQUIRE(format.get_trim_chars().empty());
    char delimiter = '\0';
    REQUIRE(format.try_get_delim(delimiter, error));
    REQUIRE(delimiter == '|');
    REQUIRE_FALSE(error);
    REQUIRE_FALSE(format.try_chunk_size(1, error));
    REQUIRE(error.code == CSVErrorCode::InvalidChunkSize);
    REQUIRE(format.get_chunk_size() == internals::CSV_CHUNK_SIZE_DEFAULT);
    REQUIRE(format.try_quote('\'', error));
    REQUIRE_FALSE(error);
    REQUIRE_FALSE(format.try_delimiter(std::vector<char>{}, error));
    REQUIRE(error);
    REQUIRE(format.try_get_delim(delimiter, error));
    REQUIRE(delimiter == '|');
    REQUIRE_FALSE(error);
}

TEST_CASE("Reader factories report initialization failure without partial objects", "[explicit_errors]") {
    CSVError error;
    SECTION("Missing filename") {
        auto reader = CSVReader::try_open(
            "tests/data/fake_data/explicit_errors_missing_directory/file.csv", error);
        REQUIRE_FALSE(reader);
        REQUIRE(error.code == CSVErrorCode::OpenFailed);
        REQUIRE_FALSE(error.message().empty());
    }
    SECTION("Failed borrowed stream") {
        std::ifstream input("tests/data/fake_data/explicit_errors_missing_directory/file.csv");
        auto reader = CSVReader::try_open(input, error);
        REQUIRE_FALSE(reader);
        REQUIRE(error);
    }
    SECTION("Null owned stream") {
        auto reader = CSVReader::try_open(std::unique_ptr<std::istream>(), error);
        REQUIRE_FALSE(reader);
        REQUIRE(error.code == CSVErrorCode::InvalidArgument);
    }
}

TEST_CASE("Explicit row reads distinguish missing fields, failure and EOF", "[explicit_errors]") {
    FileGuard file("tests/data/fake_data/explicit_errors_rows.csv");
    { std::ofstream out(file.filename); out << "left,right\n11,29\n"; }
    CSVError error;
    const auto validate = [&](CSVReader& reader) {
        CSVRow row;
        REQUIRE(reader.try_read_row(row, error));
        REQUIRE_FALSE(error);
        row.try_field("missing", error);
        REQUIRE(error.code == CSVErrorCode::ColumnNotFound);
        auto field = row.try_field("right", error);
        REQUIRE_FALSE(error);
        int value = 0;
        REQUIRE(field.try_get(value));
        REQUIRE(value == 29);
        row.try_field(size_t{2}, error);
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
        REQUIRE_FALSE(reader.try_read_row(row, error));
        REQUIRE_FALSE(error);
    };
    SECTION("Memory-mapped file") {
        auto reader = CSVReader::try_open(file.filename, error);
        REQUIRE(reader);
        validate(*reader);
    }
    SECTION("Stream") {
        std::ifstream input(file.filename, std::ios::binary);
        auto reader = CSVReader::try_open(input, error);
        REQUIRE(reader);
        validate(*reader);
    }
}

TEST_CASE("Explicit reads preserve distinct fields across 10MB", "[explicit_errors][chunk_boundary]") {
    // Catches a no-exception path accidentally bypassing chunk ownership or
    // consumer filtering while preserving all 500K distinct-value rows.
    const bool threaded = GENERATE(false, true);
    const std::string& filename = large_csv(false);
    CSVError error;
    const CSVFormat format = large_format(threaded);
    const auto validate = [&](CSVReader& reader) {
        std::vector<CSVRow> rows;
        size_t count = 0;
        while (reader.try_read_chunk(rows, 8192, error)) {
            for (const auto& row : rows) {
                if (row.size() != 5) FAIL("A row lost fields at a chunk boundary");
                for (size_t column = 0; column < 5; ++column) {
                    auto field = row.try_field(column, error);
                    size_t value = 0;
                    if (error || !field.try_get(value) || value != count * 5 + column) {
                        CAPTURE(count, column, value);
                        FAIL("A field was corrupted at a chunk boundary");
                    }
                }
                ++count;
            }
        }
        REQUIRE_FALSE(error);
        REQUIRE(count == large_row_count);
    };
    SECTION("Memory-mapped file") {
        auto reader = CSVReader::try_open(filename, error, format);
        REQUIRE(reader);
        validate(*reader);
    }
    SECTION("Stream") {
        std::ifstream input(filename, std::ios::binary);
        auto reader = CSVReader::try_open(input, error, format);
        REQUIRE(reader);
        validate(*reader);
    }
}

TEST_CASE("Explicit reads report late row errors and remain failed", "[explicit_errors][chunk_boundary]") {
    // The malformed row lies after 500K rows, beyond the first 10MB read.
    const bool threaded = GENERATE(false, true);
    const std::string& filename = large_csv(true);
    CSVError error;
    const CSVFormat format = large_format(threaded);
    const auto validate = [&](CSVReader& reader) {
        CSVRow row;
        size_t count = 0;
        while (reader.try_read_row(row, error)) ++count;
        REQUIRE(count == large_row_count);
        REQUIRE(error.code == CSVErrorCode::RowLengthMismatch);
        const std::string message = error.message();
        REQUIRE_FALSE(reader.try_read_row(row, error));
        REQUIRE(error.code == CSVErrorCode::RowLengthMismatch);
        REQUIRE(error.message() == message);
    };
    SECTION("Memory-mapped file") {
        auto reader = CSVReader::try_open(filename, error, format);
        REQUIRE(reader);
        validate(*reader);
    }
    SECTION("Stream") {
        std::ifstream input(filename, std::ios::binary);
        auto reader = CSVReader::try_open(input, error, format);
        REQUIRE(reader);
        validate(*reader);
    }
}

TEST_CASE("A worker read failure is returned after the initial window", "[explicit_errors][chunk_boundary]") {
    const bool threaded = GENERATE(false, true);
    LateFailStream input(large_csv(false));
#if CSV_EXCEPTIONS_ENABLED
    // Exercise both badbit reporting and a throwing borrowed stream in the worker.
    if (GENERATE(false, true)) input.exceptions(std::ios::badbit);
#endif
    CSVError error;
    auto reader = CSVReader::try_open(input, error, large_format(threaded));
    REQUIRE(reader);
    CSVRow row;
    size_t count = 0;
    while (reader->try_read_row(row, error)) ++count;
    REQUIRE(count > 0);
    REQUIRE(count < large_row_count);
    REQUIRE(error.code == CSVErrorCode::ReadFailed);
    REQUIRE_FALSE(reader->try_read_row(row, error));
    REQUIRE(error.code == CSVErrorCode::ReadFailed);
}

TEST_CASE("Explicit DataFrame operations preserve edits and report invalid operations", "[explicit_errors][data_frame]") {
    std::istringstream input("id,name,value\n1,Alice,10\n2,Bob,20\n");
    CSVError error;
    auto reader = CSVReader::try_open(input, error);
    REQUIRE(reader);
    auto frame = DataFrame<>::try_create(*reader, error);
    REQUIRE(frame);
    auto row = frame->try_at(0, error);
    auto cell = row.try_field("name", error);
    REQUIRE_FALSE(error);
    REQUIRE(cell.try_assign("Alicia", error));
    REQUIRE(frame->try_append_column("extra", "new", error));
    row = frame->try_at(0, error);
    cell = row.try_field("name", error);
    REQUIRE_FALSE(error);
    REQUIRE(cell.get_sv() == "Alicia");
    auto added = row.try_field("extra", error);
    REQUIRE(added.get_sv() == "new");
    REQUIRE_FALSE(frame->try_insert_row(99, std::vector<std::string>{}, error));
    REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
    REQUIRE_FALSE(frame->try_append_column("name", error));
    REQUIRE(error);
    REQUIRE(frame->n_cols() == 4);
    frame->try_at(99, error);
    REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
    const auto& read_only = *frame;
    auto const_row = read_only.try_at(0, error);
    auto const_cell = const_row.try_field("name", error);
    REQUIRE_FALSE(const_cell.try_assign("bad", error));
    REQUIRE(error.code == CSVErrorCode::ReadOnlyProxy);
    auto column = frame->try_column_view("name", error);
    REQUIRE_FALSE(error);
    REQUIRE(frame->try_append_column("last", error));
    column.try_get_sv(0, error);
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
}

TEST_CASE("Explicit keyed DataFrames retain key and conversion failure categories", "[explicit_errors][data_frame]") {
    CSVError error;
    DataFrameOptions options;
    options.set_key_column("id").set_duplicate_key_policy(DataFrameOptions::DuplicateKeyPolicy::THROW);
    SECTION("Duplicate key") {
        std::istringstream input("id,value\n1,10\n1,20\n");
        auto reader = CSVReader::try_open(input, error);
        REQUIRE(reader);
        auto frame = DataFrame<>::try_create(*reader, options, error);
        REQUIRE_FALSE(frame);
        REQUIRE(error.code == CSVErrorCode::DuplicateKey);
    }
    SECTION("Numeric key conversion") {
        std::istringstream input("id,value\nbad,10\n");
        auto reader = CSVReader::try_open(input, error);
        REQUIRE(reader);
        auto frame = DataFrame<int>::try_create(*reader, options, error);
        REQUIRE_FALSE(frame);
        REQUIRE(error.code == CSVErrorCode::ConversionFailed);
        REQUIRE(error.conversion == CSVConversionError::NotANumber);
    }
    SECTION("Key lookup and protected key column") {
        std::istringstream input("id,value\n1,10\n2,20\n");
        auto reader = CSVReader::try_open(input, error);
        REQUIRE(reader);
        auto frame = DataFrame<>::try_create(*reader, options, error);
        REQUIRE(frame);
        frame->try_find("missing", error);
        REQUIRE(error.code == CSVErrorCode::KeyNotFound);
        auto row = frame->try_find("2", error);
        REQUIRE_FALSE(error);
        REQUIRE(row.try_field("value", error).get_sv() == "20");
        auto key_column = frame->try_column_view("id", error);
        REQUIRE_FALSE(key_column.try_erase(error));
        REQUIRE(error.code == CSVErrorCode::KeyColumnProtected);
    }
}

TEST_CASE("Explicit writers report write flush and file-opening failures", "[explicit_errors][writer]") {
    CSVError error;
    SECTION("Write failure") {
        FailingWriteBuffer buffer;
        std::ostream output(&buffer);
        CSVWriter<std::ostream> writer(output);
        REQUIRE_FALSE(writer.try_write_row(error, "left", 23));
        REQUIRE(error.code == CSVErrorCode::WriteFailed);
    }
    SECTION("Flush failure") {
        FailingFlushBuffer buffer;
        std::ostream output(&buffer);
        CSVWriter<std::ostream> writer(output);
        writer.set_auto_flush(false);
        REQUIRE(writer.try_write_row(error, "left", 23));
        REQUIRE_FALSE(error);
        REQUIRE_FALSE(writer.try_flush(error));
        REQUIRE(error.code == CSVErrorCode::WriteFailed);
    }
    SECTION("Missing output directory") {
        auto writer = CSVWriter<std::ofstream>::try_open(
            "tests/data/fake_data/explicit_errors_missing_directory/out.csv", error);
        REQUIRE_FALSE(writer);
        REQUIRE(error.code == CSVErrorCode::OpenFailed);
    }
    SECTION("Borrowed stream exception settings are preserved") {
        FailingWriteBuffer buffer;
        std::ostream output(&buffer);
        output.exceptions(std::ios::badbit);
        CSVWriter<std::ostream> writer(output);
        REQUIRE_FALSE(writer.try_write_row(error, "left", 23));
#if CSV_EXCEPTIONS_ENABLED
        REQUIRE(error.code == CSVErrorCode::WriteFailed);
#else
        REQUIRE(error.code == CSVErrorCode::InvalidArgument);
#endif
        REQUIRE(output.exceptions() == std::ios::badbit);
    }
    SECTION("Owned output can be explicitly completed") {
        FileGuard file("tests/data/fake_data/explicit_errors_output.csv");
        auto writer = CSVWriter<std::ofstream>::try_open(file.filename, error);
        REQUIRE(writer);
        REQUIRE(writer->try_write_row(error, "a,b", "quoted\"value"));
        REQUIRE(writer->try_close(error));
        REQUIRE_FALSE(error);
        std::ifstream input(file.filename);
        std::stringstream content;
        content << input.rdbuf();
        REQUIRE(content.str() == "\"a,b\",\"quoted\"\"value\"\n");
    }
}

TEST_CASE("Checked DataFrame writing reports short native rows", "[explicit_errors][writer][data_frame]") {
    // KEEP intentionally retains a row shorter than the visible schema. The
    // checked writer must report it rather than asserting in a row view.
    std::istringstream input("a,b\n1\n");
    CSVFormat format;
    format.header_row(0).variable_columns(VariableColumnPolicy::KEEP);
    CSVError error;
    auto reader = CSVReader::try_open(input, error, format);
    REQUIRE(reader);
    auto frame = DataFrame<>::try_create(*reader, error);
    REQUIRE(frame);
    std::ostringstream output;
    CSVWriter<std::ostringstream> writer(output);
    SECTION("Complete frame including header") {
        REQUIRE_FALSE(writer.try_write_row(error, *frame));
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
    }
#ifdef CSV_HAS_CXX20
    SECTION("Bulk visible rows") {
        REQUIRE_FALSE(writer.try_write_rows(*frame, error));
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
    }
#endif
}

TEST_CASE("Moving a closed writer does not reopen its output", "[explicit_errors][writer]") {
    FileGuard first("tests/data/fake_data/explicit_errors_closed_first.csv");
    FileGuard second("tests/data/fake_data/explicit_errors_closed_second.csv");
    CSVError error;
    auto writer = CSVWriter<std::ofstream>::try_open(first.filename, error);
    REQUIRE(writer);
    REQUIRE(writer->try_close(error));
    CSVWriter<std::ofstream> moved(std::move(*writer));
    require_closed_writer(moved);
    auto assigned = CSVWriter<std::ofstream>::try_open(second.filename, error);
    REQUIRE(assigned);
    *assigned = std::move(moved);
    require_closed_writer(*assigned);
}

#ifdef __linux__
TEST_CASE("Moving a writer after failed close keeps it closed", "[explicit_errors][writer]") {
    // /dev/full provides a deterministic owned-file write failure on Linux.
    CSVError error;
    auto writer = CSVWriter<std::ofstream>::try_open("/dev/full", error);
    REQUIRE(writer);
    writer->set_auto_flush(false);
    REQUIRE(writer->try_write_row(error, "buffered", 1));
    REQUIRE_FALSE(writer->try_close(error));
    REQUIRE(error.code == CSVErrorCode::WriteFailed);
    CSVWriter<std::ofstream> moved(std::move(*writer));
    require_closed_writer(moved);
    FileGuard file("tests/data/fake_data/explicit_errors_closed_after_failure.csv");
    auto assigned = CSVWriter<std::ofstream>::try_open(file.filename, error);
    REQUIRE(assigned);
    *assigned = std::move(moved);
    require_closed_writer(*assigned);
}
#endif

TEST_CASE("Type inference stops after the first failed batch", "[explicit_errors][chunk_boundary]") {
    // A short KEEP row must fail inference before consuming the remaining
    // 500K-row source, including when a worker prefetched later windows.
    const std::string& filename = large_csv(false, true);
    CSVFormat format;
    format.header_row(0).variable_columns(VariableColumnPolicy::KEEP);
    CSVError error;
    const auto validate = [&](CSVReader& reader) {
        auto types = try_csv_data_types(reader, error);
        REQUIRE(types.empty());
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
        REQUIRE(reader.n_rows() <= 5000);
    };
    SECTION("Memory-mapped file") {
        auto reader = CSVReader::try_open(filename, error, format);
        REQUIRE(reader);
        validate(*reader);
    }
    SECTION("Stream") {
        std::ifstream input(filename, std::ios::binary);
        auto reader = CSVReader::try_open(input, error, format);
        REQUIRE(reader);
        validate(*reader);
    }
}

TEST_CASE("Explicit file helpers distinguish absent columns from I/O failures", "[explicit_errors]") {
    FileGuard file("tests/data/fake_data/explicit_errors_helpers.csv");
    { std::ofstream out(file.filename); out << "left,right\n3,7\n"; }
    CSVError error;
    REQUIRE(try_get_col_pos(file.filename, "missing", error) == CSV_NOT_FOUND);
    REQUIRE_FALSE(error);
    REQUIRE(try_get_col_pos(file.filename, "right", error) == 1);
    REQUIRE_FALSE(error);
    auto info = try_get_file_info(file.filename, error);
    REQUIRE_FALSE(error);
    REQUIRE(info.n_rows == 1);
    REQUIRE(info.n_cols == 2);
}

TEST_CASE("Checked column names reject out-of-range proxy positions", "[explicit_errors][data_frame]") {
    // Public proxy construction must not turn name lookup into unchecked vector access,
    // including an empty frame where checked writing otherwise emits an empty record.
    DataFrame<> frame;
    DataFrameColumn<std::string> column(&frame, 999);
    CSVError error;
    REQUIRE_FALSE(column.is_valid());
    REQUIRE(column.try_name(error).empty());
    REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
#if CSV_EXCEPTIONS_ENABLED
    REQUIRE_THROWS_AS(column.name(), std::out_of_range);
#endif
    std::ostringstream output;
    CSVWriter<std::ostringstream> writer(output);
    REQUIRE_FALSE(writer.try_write_row(error, column));
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
    REQUIRE(output.str().empty());
}

TEST_CASE("Checked map serialization preserves explicit empty subsets", "[explicit_errors]") {
    // Full-map and explicit-subset overloads must keep their distinct empty-subset semantics.
    CSVError error;
    auto reader = try_parse("left,right\n3,7\n", error);
    REQUIRE(reader);
    CSVRow row;
    REQUIRE(reader->try_read_row(row, error));
    REQUIRE(row.try_to_unordered_map(error).size() == 2);
    REQUIRE_FALSE(error);
    REQUIRE(row.try_to_unordered_map(error, {}).empty());
    REQUIRE_FALSE(error);
    REQUIRE(CSVRow().try_to_unordered_map(error).empty());
    REQUIRE_FALSE(error);
#if CSV_EXCEPTIONS_ENABLED
    REQUIRE(row.to_unordered_map({}).empty());
#endif
}

TEST_CASE("Checked format conflicts preserve quote and delimiter settings", "[explicit_errors]") {
    CSVFormat format;
    CSVError error;
    REQUIRE_FALSE(format.try_quote(',', error));
    REQUIRE(error.code == CSVErrorCode::InvalidFormat);
    REQUIRE(format.get_quote_char() == '"');
    REQUIRE(format.try_trim({' '}, error));
    REQUIRE_FALSE(format.try_quote(' ', error));
    REQUIRE(error.code == CSVErrorCode::InvalidFormat);
    REQUIRE(format.try_delimiter(std::vector<char>{',', '|'}, error));
    char delimiter = '?';
    REQUIRE_FALSE(format.try_get_delim(delimiter, error));
    REQUIRE(error.code == CSVErrorCode::InvalidFormat);
    REQUIRE(delimiter == '?');
}

TEST_CASE("Checked reads reject zero chunks without consuming rows", "[explicit_errors]") {
    CSVError error;
    auto reader = try_parse("a,b\n3,7\n", error);
    REQUIRE(reader);
    std::vector<CSVRow> rows;
    REQUIRE_FALSE(reader->try_read_chunk(rows, 0, error));
    REQUIRE(error.code == CSVErrorCode::InvalidArgument);
    std::vector<size_t> states(2);
    const auto count = [](const DataFrameColumn<std::string>& column, size_t& state) {
        state += column.size();
    };
    REQUIRE_FALSE(try_chunk_parallel_apply(*reader, states, count, error, 0));
    REQUIRE(error.code == CSVErrorCode::InvalidArgument);
    REQUIRE(reader->n_rows() == 0);
    REQUIRE(try_chunk_parallel_apply(*reader, states, count, error, 1));
    REQUIRE_FALSE(error);
    REQUIRE(states == std::vector<size_t>{1, 1});
}

TEST_CASE("Checked stream reads distinguish throwing EOF from hard errors", "[explicit_errors]") {
    CSVError error;
    SECTION("EOF exception mask") {
        std::istringstream input("a,b\n3,7\n");
        input.exceptions(std::ios::failbit | std::ios::badbit);
        auto reader = CSVReader::try_open(input, error);
#if CSV_EXCEPTIONS_ENABLED
        REQUIRE(reader);
        CSVRow row;
        REQUIRE(reader->try_read_row(row, error));
        REQUIRE(row.try_field("b", error).get_sv() == "7");
        REQUIRE_FALSE(reader->try_read_row(row, error));
        REQUIRE_FALSE(error);
#else
        REQUIRE_FALSE(reader);
        REQUIRE(error.code == CSVErrorCode::InvalidArgument);
#endif
        REQUIRE(input.exceptions() == (std::ios::failbit | std::ios::badbit));
    }
#if CSV_EXCEPTIONS_ENABLED
    SECTION("Initial read failure") {
        FailingReadBuffer buffer;
        std::istream input(&buffer);
        input.exceptions(std::ios::badbit);
        REQUIRE_FALSE(CSVReader::try_open(input, error));
        REQUIRE(error.code == CSVErrorCode::ReadFailed);
        REQUIRE(input.exceptions() == std::ios::badbit);
    }
    SECTION("Owned stream constructor uses the same initialization") {
        std::unique_ptr<std::istream> input(new std::istringstream("a,b\n3,7\n"));
        CSVReader reader(std::move(input));
        CSVRow row;
        REQUIRE(reader.read_row(row));
        REQUIRE(row["b"].get<int>() == 7);
    }
#endif
}

TEST_CASE("Checked column conversions keep partial values and the precise cause", "[explicit_errors][data_frame]") {
    CSVError error;
    auto reader = try_parse("id,value\n1,17\n2,text\n", error);
    REQUIRE(reader);
    auto frame = DataFrame<>::try_create(*reader, error);
    REQUIRE(frame);
    const auto& view = *frame;
    const auto column = view.try_column_view("value", error);
    REQUIRE(column.try_name(error) == "value");
    auto values = column.try_to_vector<int>(error);
    REQUIRE(values == std::vector<int>{17});
    REQUIRE(error.code == CSVErrorCode::ConversionFailed);
    REQUIRE(error.conversion == CSVConversionError::NotANumber);
    column.try_field(99, error);
    REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
    auto read_only_cell = column.try_field(0, error);
    REQUIRE_FALSE(read_only_cell.try_assign("changed", error));
    REQUIRE(error.code == CSVErrorCode::ReadOnlyProxy);
    auto read_only_column = view.try_column_view("value", error);
    REQUIRE_FALSE(read_only_column.try_erase(error));
    REQUIRE(error.code == CSVErrorCode::ReadOnlyProxy);
    REQUIRE(frame->try_column<std::string>("missing", error).empty());
    REQUIRE(error.code == CSVErrorCode::ColumnNotFound);
    REQUIRE_FALSE(frame->try_contains("unused", error));
    REQUIRE(error);
#if CSV_EXCEPTIONS_ENABLED
    REQUIRE_THROWS_AS(column[99], std::out_of_range);
#endif
}

TEST_CASE("Checked unbound proxies fail before accessing storage", "[explicit_errors][data_frame]") {
    CSVError error;
    DataFrameRow<std::string> row;
    const auto& const_row = row;
    row.try_field(size_t{0}, error);
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
    const_row.try_field("missing", error);
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
    REQUIRE(row.try_to_json({}, error).empty());
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
    REQUIRE(row.try_to_json_array({"missing"}, error).empty());
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
    DataFrameColumn<std::string> column;
    const auto& const_column = column;
    const_column.try_field(0, error);
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
    REQUIRE(column.try_get_sv(0, error).empty());
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
    REQUIRE(column.try_to_vector(error).empty());
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
    REQUIRE(column.try_name(error).empty());
    REQUIRE(error.code == CSVErrorCode::InvalidProxy);
}

TEST_CASE("Checked key insertion validates conversions before mutation", "[explicit_errors][data_frame]") {
    CSVError error;
    auto reader = try_parse("id,value\n1,17\n", error);
    REQUIRE(reader);
    DataFrameOptions options;
    options.set_key_column("id");
    auto frame = DataFrame<int>::try_create(*reader, options, error);
    REQUIRE(frame);
    REQUIRE_FALSE(frame->try_insert_row(1, {"text", "29"}, error));
    REQUIRE(error.code == CSVErrorCode::ConversionFailed);
    REQUIRE(error.conversion == CSVConversionError::NotANumber);
    REQUIRE(frame->size() == 1);
    REQUIRE(frame->try_insert_row(1, {"2", "29"}, error));
    const auto& view = *frame;
    REQUIRE(view.try_contains(2, error));
    REQUIRE_FALSE(error);
    REQUIRE(view.try_find(2, error).try_field("value", error).get_sv() == "29");
    view.try_find(99, error);
    REQUIRE(error.code == CSVErrorCode::KeyNotFound);
    view.try_at(99, error);
    REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
}

TEST_CASE("Optional numeric keys use the default key on conversion failure", "[explicit_errors][data_frame]") {
    CSVError error;
    auto reader = try_parse("id,value\ntext,17\n2,29\n", error);
    REQUIRE(reader);
    DataFrameOptions options;
    options.set_key_column("id").set_throw_on_missing_key(false);
    auto frame = DataFrame<int>::try_create(*reader, options, error);
    REQUIRE(frame);
    REQUIRE_FALSE(error);
    REQUIRE(frame->try_find(0, error).try_field("value", error).get_sv() == "17");
    REQUIRE(frame->try_find(2, error).try_field("value", error).get_sv() == "29");
}

TEST_CASE("Checked serialization reports unknown names and mismatched widths", "[explicit_errors]") {
    CSVError error;
    CSVFormat format;
    format.header_row(0).variable_columns(VariableColumnPolicy::KEEP);
    auto reader = try_parse("a,b\n3,7,11\n", error, format);
    REQUIRE(reader);
    CSVRow row;
    REQUIRE(reader->try_read_row(row, error));
    REQUIRE(row.try_to_unordered_map(error).empty());
    REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
    REQUIRE(row.try_to_unordered_map(error, {"missing"}).empty());
    REQUIRE(error.code == CSVErrorCode::ColumnNotFound);
    REQUIRE(row.try_to_json({"missing"}, error).empty());
    REQUIRE(error.code == CSVErrorCode::ColumnNotFound);
    REQUIRE(row.try_to_json_array({"missing"}, error).empty());
    REQUIRE(error.code == CSVErrorCode::ColumnNotFound);
#if CSV_EXCEPTIONS_ENABLED
    REQUIRE_THROWS_AS(row.to_unordered_map(), std::out_of_range);
#endif
}

TEST_CASE("Checked DataFrame writing retains edits and escapes every record", "[explicit_errors][writer][data_frame]") {
    CSVError error;
    auto reader = try_parse("id,value\n1,17\n2,29\n", error);
    REQUIRE(reader);
    auto frame = DataFrame<>::try_create(*reader, error);
    REQUIRE(frame);
    auto row = frame->try_at(0, error);
    auto cell = row.try_field("value", error);
    REQUIRE(cell.try_assign("x,y", error));
    auto column = frame->try_column_view("value", error);
    std::ostringstream output;
    CSVWriter<std::ostringstream> writer(output);
    writer.set_auto_flush(false);
    REQUIRE(writer.try_write_row(error, *frame));
    REQUIRE(writer.try_write_row(error, row));
    REQUIRE(writer.try_write_row(error, column));
    REQUIRE(writer.try_flush(error));
    REQUIRE_FALSE(error);
    REQUIRE(output.str() == "id,value\n1,\"x,y\"\n2,29\n1,\"x,y\"\n\"x,y\",29\n");
}

TEST_CASE("Checked writers reject failed and borrowed close operations", "[explicit_errors][writer]") {
    CSVError error;
    SECTION("Already failed output") {
        std::ostringstream output;
        output.setstate(std::ios::badbit);
        CSVWriter<std::ostringstream> writer(output);
        REQUIRE_FALSE(writer.try_write_row(error, "left", 23));
        REQUIRE(error.code == CSVErrorCode::WriteFailed);
        REQUIRE(output.str().empty());
    }
    SECTION("Borrowed file and repeated close") {
        FileGuard file("tests/data/fake_data/explicit_errors_borrowed_close.csv");
        std::ofstream output(file.filename);
        CSVWriter<std::ofstream> writer(output);
        REQUIRE_FALSE(writer.try_close(error));
        REQUIRE(error.code == CSVErrorCode::InvalidArgument);
        REQUIRE(output.is_open());
        REQUIRE(writer.try_write_row(error, "left", 23));
        FileGuard owned_file("tests/data/fake_data/explicit_errors_repeated_close.csv");
        auto owned = CSVWriter<std::ofstream>::try_open(owned_file.filename, error);
        REQUIRE(owned);
        REQUIRE(owned->try_close(error));
        REQUIRE_FALSE(owned->try_close(error));
        REQUIRE(error.code == CSVErrorCode::InvalidArgument);
    }
#if CSV_EXCEPTIONS_ENABLED
    SECTION("Destructor contains a buffered stream exception") {
        FailingWriteBuffer buffer;
        std::ostream output(&buffer);
        output.exceptions(std::ios::badbit);
        {
            CSVWriter<std::ostream> writer(output);
            writer.set_auto_flush(false);
            REQUIRE(writer.try_write_row(error, "buffered", 23));
        }
        REQUIRE(output.bad());
    }
#endif
}

TEST_CASE("Checked file helpers retain opening failures", "[explicit_errors]") {
    const std::string missing = "tests/data/fake_data/explicit_errors_missing_directory/file.csv";
    CSVError error;
    try_get_file_info(missing, error);
    REQUIRE(error.code == CSVErrorCode::OpenFailed);
    REQUIRE(try_get_col_names(missing, error).empty());
    REQUIRE(error.code == CSVErrorCode::OpenFailed);
    REQUIRE(try_get_col_pos(missing, "a", error) == CSV_NOT_FOUND);
    REQUIRE(error.code == CSVErrorCode::OpenFailed);
    REQUIRE(try_csv_data_types(missing, error).empty());
    REQUIRE(error.code == CSVErrorCode::OpenFailed);
}

TEST_CASE("Checked DataFrame operations validate retained short rows", "[explicit_errors][data_frame]") {
    // KEEP rows have a wider visible schema than their physical storage. These
    // APIs must report the missing field before serialization or materialization.
    FileGuard file("tests/data/fake_data/explicit_errors_short_operations.csv");
    { std::ofstream out(file.filename); out << "a,b\n1,17\n2\n"; }
    CSVFormat format;
    format.header_row(0).variable_columns(VariableColumnPolicy::KEEP);
    CSVError error;
    const auto validate = [&](CSVReader& reader) {
        auto frame = DataFrame<>::try_create(reader, error);
        REQUIRE(frame);
        auto row = frame->try_at(1, error);
        REQUIRE(row.try_to_json({}, error).empty());
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
        REQUIRE(row.try_to_json_array({"b"}, error).empty());
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
        REQUIRE(row.try_to_json({"missing"}, error).empty());
        REQUIRE(error.code == CSVErrorCode::ColumnNotFound);
        auto column = frame->try_column_view("b", error);
        REQUIRE(column.try_to_vector<int>(error) == std::vector<int>{17});
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
        REQUIRE(frame->try_group_by("b", error).empty());
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
        REQUIRE_FALSE(frame->try_append_column("extra", "new", error));
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
        REQUIRE(frame->n_cols() == 2);
        REQUIRE(frame->size() == 2);
        auto valid = frame->try_at(0, error).try_field("b", error);
        REQUIRE(valid.get_sv() == "17");
        REQUIRE(valid.try_assign("edited", error));
        // Exercise the same validation after promotion to sparse edit storage.
        REQUIRE_FALSE(frame->try_append_column("extra", error));
        REQUIRE(error.code == CSVErrorCode::IndexOutOfBounds);
        REQUIRE(frame->try_at(0, error).try_field("b", error).get_sv() == "edited");
    };
    SECTION("Memory-mapped file") {
        auto reader = CSVReader::try_open(file.filename, error, format);
        REQUIRE(reader);
        validate(*reader);
    }
    SECTION("Stream") {
        std::ifstream input(file.filename, std::ios::binary);
        auto reader = CSVReader::try_open(input, error, format);
        REQUIRE(reader);
        validate(*reader);
    }
}

TEST_CASE("Internal BOM detection reports encoding errors", "[explicit_errors][read_unicode_bom]") {
    CSVError error(CSVErrorCode::ReadFailed, "previous failure");
    bool utf8_bom = true;
    SECTION("UTF-8 clears an earlier error") {
        REQUIRE(internals::get_bom_skip(csv::string_view("\xEF\xBB\xBF" "A,B\n", 7), utf8_bom, error) == 3);
        REQUIRE_FALSE(error);
        REQUIRE(utf8_bom);
    }
    SECTION("UTF-16") {
        REQUIRE(internals::get_bom_skip(csv::string_view("\xFF\xFE" "A\0", 4), utf8_bom, error) == 0);
        REQUIRE(error.code == CSVErrorCode::UnsupportedEncoding);
        REQUIRE(error.context.find("UTF-16") != std::string::npos);
        REQUIRE_FALSE(utf8_bom);
    }
    SECTION("UTF-32") {
        REQUIRE(internals::get_bom_skip(csv::string_view("\x00\x00\xFE\xFF", 4), utf8_bom, error) == 0);
        REQUIRE(error.code == CSVErrorCode::UnsupportedEncoding);
        REQUIRE(error.context.find("UTF-32") != std::string::npos);
        REQUIRE_FALSE(utf8_bom);
    }
}

TEST_CASE("Format guessing translates errors only at the public boundary", "[explicit_errors][guess_format]") {
    FileGuard file("tests/data/fake_data/explicit_errors_guess_format.csv");
    { std::ofstream out(file.filename); out << "left|right\n3|7\n"; }
    CSVError error;
    SECTION("Successful format") {
        auto format = try_guess_format(file.filename, error, {'|'});
        REQUIRE_FALSE(error);
        REQUIRE(format.delim == '|');
        REQUIRE(format.header_row == 0);
        REQUIRE(format.n_cols == 2);
#if CSV_EXCEPTIONS_ENABLED
        auto throwing = guess_format(file.filename, {'|'});
        REQUIRE(throwing.delim == format.delim);
        REQUIRE(throwing.header_row == format.header_row);
        REQUIRE(throwing.n_cols == format.n_cols);
#endif
    }
    SECTION("Missing source") {
        const auto missing = file.filename + ".missing";
        try_guess_format(missing, error);
        REQUIRE(error.code == CSVErrorCode::OpenFailed);
#if CSV_EXCEPTIONS_ENABLED
        REQUIRE_THROWS_AS(guess_format(missing), std::runtime_error);
#endif
    }
    SECTION("No delimiter candidates") {
        try_guess_format(file.filename, error, {});
        REQUIRE(error.code == CSVErrorCode::InvalidFormat);
#if CSV_EXCEPTIONS_ENABLED
        REQUIRE_THROWS_AS(guess_format(file.filename, {}), std::runtime_error);
#endif
    }
    SECTION("Unsupported encoding") {
        {
            std::ofstream out(file.filename, std::ios::binary);
            const char bytes[] = {'\xFF', '\xFE', 'A', '\0'};
            out.write(bytes, sizeof(bytes));
        }
        try_guess_format(file.filename, error);
        REQUIRE(error.code == CSVErrorCode::UnsupportedEncoding);
#if CSV_EXCEPTIONS_ENABLED
        REQUIRE_THROWS_WITH(guess_format(file.filename), Catch::Matchers::ContainsSubstring("UTF-16"));
#endif
    }
}
