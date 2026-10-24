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
