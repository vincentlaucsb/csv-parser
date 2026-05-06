#include <catch2/catch_all.hpp>
#include "internal/csv_speculative_parser.hpp"
#include "internal/csv_row.hpp"
#include "internal/stream_parser.hpp"
#include "shared/file_guard.hpp"

#include <fstream>
#include <sstream>

using namespace csv;
using namespace csv::internals;
using namespace csv::internals::speculative;

#if CSV_ENABLE_THREADS
TEST_CASE("Speculative scanner classifies obvious outside chunks", "[raw_csv_parse][speculative]") {
    SpeculativeScanner scanner(internals::make_parse_flags(',', '"'));
    const std::string chunk =
        "a,b,c\n"
        "1,2,3\n";

    const auto speculation = scanner.speculate(3, 4096, chunk);

    REQUIRE(speculation.sequence_number == 3);
    REQUIRE(speculation.offset == 4096);
    REQUIRE(speculation.length == chunk.size());
    REQUIRE(speculation.prefix_length == chunk.size());
    REQUIRE_FALSE(speculation.assumed_start_state.quote_escape);
    REQUIRE_FALSE(speculation.assumed_start_state.pending_quote);
    REQUIRE_FALSE(speculation.ambiguous);
    REQUIRE(speculation.outside_scan.records_seen == 2);
    REQUIRE(speculation.inside_scan.records_seen == 0);
}

TEST_CASE("Speculative scanner uses quote-boundary evidence before probability", "[raw_csv_parse][speculative]") {
    SpeculativeScanner scanner(internals::make_parse_flags(',', '"'));
    const std::string chunk =
        "continued text\n"
        "still quoted\",tail\n";

    const auto speculation = scanner.speculate(4, 8192, chunk);

    REQUIRE(speculation.assumed_start_state.quote_escape);
    REQUIRE_FALSE(speculation.ambiguous);
    REQUIRE_FALSE(speculation.used_probability_model);
    REQUIRE(speculation.outside_scan.records_seen > 0);
    REQUIRE(speculation.inside_scan.records_seen > 0);
}

TEST_CASE("Speculative scanner leaves ordinary quoted rows unquoted", "[raw_csv_parse][speculative]") {
    SpeculativeScanner scanner(internals::make_parse_flags(',', '"'));
    const std::string chunk =
        "a,\"b,c\"\n"
        "1,2\n";

    const auto speculation = scanner.speculate(5, 16384, chunk);

    REQUIRE_FALSE(speculation.assumed_start_state.quote_escape);
    REQUIRE_FALSE(speculation.ambiguous);
    REQUIRE_FALSE(speculation.used_probability_model);
    REQUIRE(speculation.outside_scan.first_quote_open < speculation.inside_scan.first_quote_close);
}

TEST_CASE("Speculative scanner uses probability model for unresolved embedded-quote ambiguity", "[raw_csv_parse][speculative]") {
    SpeculativeScanner scanner(internals::make_parse_flags(',', '"'));
    const std::string chunk =
        "ab\"c\n"
        "d\"ef\n"
        "gh\n";

    const auto speculation = scanner.speculate(6, 32768, chunk);

    REQUIRE(speculation.ambiguous);
    REQUIRE(speculation.used_probability_model);
    REQUIRE_FALSE(speculation.used_record_size_heuristic);
    REQUIRE(speculation.quoted_start_odds < 1);
    REQUIRE_FALSE(speculation.assumed_start_state.quote_escape);
}

TEST_CASE("Speculative scanner preserves escaped quote pairs in quoted interpretation", "[raw_csv_parse][speculative]") {
    SpeculativeScanner scanner(internals::make_parse_flags(',', '"'));
    const std::string chunk = "continued \"\"quoted\"\" text\",tail\n";
    const size_t closing_quote = chunk.find("\",tail");

    const auto speculation = scanner.speculate(7, 65536, chunk);

    REQUIRE(speculation.inside_scan.first_quote_close == closing_quote);
    REQUIRE(speculation.inside_scan.records_seen == 1);
    REQUIRE_FALSE(speculation.inside_scan.ending_state.quote_escape);
}
#endif

TEST_CASE("Parsed chunk rows split edge fragments from complete rows", "[raw_csv_parse][fragments]") {
    CSVParserCore<std::vector<CSVRow>> parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    SECTION("record-boundary chunk keeps trailing partial row as suffix") {
        std::vector<CSVRow> parsed_rows;
        auto chunk = std::make_shared<std::string>("id,value\n1,\"long");

        const auto parse_result = parser.parse_chunk(*chunk, chunk, parsed_rows);
        auto rows = split_parsed_chunk_rows(
            0,
            *chunk,
            chunk,
            parse_result,
            std::move(parsed_rows),
            true
        );

        REQUIRE(rows.prefix_fragment.empty());
        REQUIRE(rows.complete_rows.size() == 1);
        REQUIRE(rows.complete_rows[0][0] == "id");
        REQUIRE(rows.suffix_fragment.bytes == "1,\"long");
        REQUIRE(rows.suffix_fragment.ending_state.quote_escape);
    }

    SECTION("continuation chunk keeps leading partial row as prefix") {
        std::vector<CSVRow> parsed_rows;
        auto chunk = std::make_shared<std::string>(" value\",2\n3,Bob\n4,\"tail");
        ParserChunkOptions options(ParserDFAState(true), false);

        const auto parse_result = parser.parse_chunk(*chunk, chunk, parsed_rows, options);
        auto rows = split_parsed_chunk_rows(
            1,
            *chunk,
            chunk,
            parse_result,
            std::move(parsed_rows),
            false
        );

        REQUIRE(rows.prefix_fragment.bytes == " value\",2");
        REQUIRE(rows.complete_rows.size() == 1);
        REQUIRE(rows.complete_rows[0][0] == "3");
        REQUIRE(rows.complete_rows[0][1] == "Bob");
        REQUIRE(rows.suffix_fragment.bytes == "4,\"tail");
        REQUIRE(rows.suffix_fragment.ending_state.quote_escape);
    }

    SECTION("continuation prefix preserves embedded newlines") {
        std::vector<CSVRow> parsed_rows;
        auto chunk = std::make_shared<std::string>("line one\nline two\",tail\nnext,row\npartial");
        ParserChunkOptions options(ParserDFAState(true), false);

        const auto parse_result = parser.parse_chunk(*chunk, chunk, parsed_rows, options);
        auto rows = split_parsed_chunk_rows(
            2,
            *chunk,
            chunk,
            parse_result,
            std::move(parsed_rows),
            false
        );

        REQUIRE(rows.prefix_fragment.bytes == "line one\nline two\",tail");
        REQUIRE(rows.complete_rows.size() == 1);
        REQUIRE(rows.complete_rows[0][0] == "next");
        REQUIRE(rows.complete_rows[0][1] == "row");
        REQUIRE(rows.suffix_fragment.bytes == "partial");
    }

    SECTION("continuation chunk without a record boundary is one prefix fragment") {
        std::vector<CSVRow> parsed_rows;
        auto chunk = std::make_shared<std::string>(" still inside the same quoted field");
        ParserChunkOptions options(ParserDFAState(true), false);

        const auto parse_result = parser.parse_chunk(*chunk, chunk, parsed_rows, options);
        auto rows = split_parsed_chunk_rows(
            2,
            *chunk,
            chunk,
            parse_result,
            std::move(parsed_rows),
            false
        );

        REQUIRE(rows.prefix_fragment.bytes == *chunk);
        REQUIRE(rows.prefix_fragment.ending_state.quote_escape);
        REQUIRE(rows.complete_rows.empty());
        REQUIRE(rows.suffix_fragment.empty());
    }
}

#if CSV_ENABLE_THREADS
TEST_CASE("Speculative validator repairs wrongly seeded continuation chunks", "[raw_csv_parse][speculative][validator]") {
    ChunkParserCore chunk0_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    ChunkParserCore chunk1_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    ChunkParserCore repair_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    auto chunk0 = std::make_shared<std::string>("id,text,status\n1,\"hello ");
    std::vector<CSVRow> parsed0;
    const auto result0 = chunk0_parser.parse_chunk(*chunk0, chunk0, parsed0);
    auto rows0 = split_parsed_chunk_rows(0, *chunk0, chunk0, result0, std::move(parsed0), true);

    auto chunk1 = std::make_shared<std::string>("world\",ok\n2,done,ok\n");
    std::vector<CSVRow> parsed1_wrong;
    const auto result1_wrong = chunk1_parser.parse_chunk(
        *chunk1,
        chunk1,
        parsed1_wrong,
        ParserChunkOptions(ParserDFAState(false), false)
    );
    auto rows1_wrong = split_parsed_chunk_rows(
        1,
        *chunk1,
        chunk1,
        result1_wrong,
        std::move(parsed1_wrong),
        false
    );

    std::vector<CSVRow> output;
    SpeculativeParseValidator<std::vector<CSVRow>> validator(repair_parser, output);

    validator.validate_and_release(std::move(rows0));
    REQUIRE(output.size() == 1);
    REQUIRE(output[0][0] == "id");

    validator.validate_and_release(std::move(rows1_wrong));
    validator.finish();

    REQUIRE(validator.repair_count() == 1);
    REQUIRE(output.size() == 3);
    REQUIRE(output[1][0] == "1");
    REQUIRE(output[1][1] == "hello world");
    REQUIRE(output[1][2] == "ok");
    REQUIRE(output[2][0] == "2");
    REQUIRE(output[2][1] == "done");
    REQUIRE(output[2][2] == "ok");
}

TEST_CASE("Speculative validator batch-releases repaired rows to RowCollection", "[raw_csv_parse][speculative][validator][row_deque]") {
    ChunkParserCore chunk0_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    ChunkParserCore chunk1_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    ChunkParserCore repair_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    auto chunk0 = std::make_shared<std::string>("id,text,status\n1,\"hello ");
    std::vector<CSVRow> parsed0;
    const auto result0 = chunk0_parser.parse_chunk(*chunk0, chunk0, parsed0);
    auto rows0 = split_parsed_chunk_rows(0, *chunk0, chunk0, result0, std::move(parsed0), true);

    auto chunk1 = std::make_shared<std::string>("world\",ok\n2,done,ok\n");
    std::vector<CSVRow> parsed1_wrong;
    const auto result1_wrong = chunk1_parser.parse_chunk(
        *chunk1,
        chunk1,
        parsed1_wrong,
        ParserChunkOptions(ParserDFAState(false), false)
    );
    auto rows1_wrong = split_parsed_chunk_rows(
        1,
        *chunk1,
        chunk1,
        result1_wrong,
        std::move(parsed1_wrong),
        false
    );

    RowCollection output;
    SpeculativeParseValidator<RowCollection> validator(repair_parser, output);

    validator.validate_and_release(std::move(rows0));
    validator.validate_and_release(std::move(rows1_wrong));
    validator.finish();

    REQUIRE(validator.repair_count() == 1);
    output.inspect([](const RowQueueInspectionView<CSVRow>& queued) {
        REQUIRE(queued.size() == 3);
        REQUIRE(queued[0][0] == "id");
        REQUIRE(queued[1][0] == "1");
        REQUIRE(queued[1][1] == "hello world");
        REQUIRE(queued[2][0] == "2");
        REQUIRE(queued[2][1] == "done");
    });
}

TEST_CASE("Speculative validator carries split rows across chunks without record boundaries", "[raw_csv_parse][speculative][validator]") {
    ChunkParserCore chunk0_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    ChunkParserCore chunk1_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    ChunkParserCore chunk2_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    ChunkParserCore repair_parser(
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    auto chunk0 = std::make_shared<std::string>("id,text,status\n1,\"alpha");
    std::vector<CSVRow> parsed0;
    const auto result0 = chunk0_parser.parse_chunk(*chunk0, chunk0, parsed0);
    auto rows0 = split_parsed_chunk_rows(0, *chunk0, chunk0, result0, std::move(parsed0), true);

    auto chunk1 = std::make_shared<std::string>(" beta");
    std::vector<CSVRow> parsed1;
    const auto result1 = chunk1_parser.parse_chunk(
        *chunk1,
        chunk1,
        parsed1,
        ParserChunkOptions(ParserDFAState(true), false)
    );
    auto rows1 = split_parsed_chunk_rows(1, *chunk1, chunk1, result1, std::move(parsed1), false);

    auto chunk2 = std::make_shared<std::string>(" gamma\",ok\n");
    std::vector<CSVRow> parsed2;
    const auto result2 = chunk2_parser.parse_chunk(
        *chunk2,
        chunk2,
        parsed2,
        ParserChunkOptions(ParserDFAState(true), false)
    );
    auto rows2 = split_parsed_chunk_rows(2, *chunk2, chunk2, result2, std::move(parsed2), false);

    std::vector<CSVRow> output;
    SpeculativeParseValidator<std::vector<CSVRow>> validator(repair_parser, output);

    validator.validate_and_release(std::move(rows0));
    REQUIRE(output.size() == 1);

    validator.validate_and_release(std::move(rows1));
    REQUIRE(output.size() == 1);

    validator.validate_and_release(std::move(rows2));
    validator.finish();

    REQUIRE(validator.repair_count() == 0);
    REQUIRE(output.size() == 2);
    REQUIRE(output[1][0] == "1");
    REQUIRE(output[1][1] == "alpha beta gamma");
    REQUIRE(output[1][2] == "ok");
}

TEST_CASE("ParallelCSVParser repairs speculative worker output in order", "[raw_csv_parse][speculative][parallel]") {
    const auto parse_flags = internals::make_parse_flags(',', '"');
    const auto ws_flags = internals::WhitespaceMap();

    auto chunk0 = std::make_shared<std::string>("id,text,status\n1,\"hello ");
    SpeculativeParseChunk first;
    first.sequence_number = 0;
    first.offset = 0;
    first.bytes = *chunk0;
    first.owner = chunk0;
    first.speculation.sequence_number = 0;
    first.speculation.assumed_start_state = ParserDFAState(false);
    first.starts_at_record_boundary = true;
    first.scan_bom = true;

    auto chunk1 = std::make_shared<std::string>("world\",ok\n2,done,ok\n");
    SpeculativeParseChunk second;
    second.sequence_number = 1;
    second.offset = chunk0->size();
    second.bytes = *chunk1;
    second.owner = chunk1;
    second.speculation.sequence_number = 1;
    second.speculation.assumed_start_state = ParserDFAState(false);
    second.starts_at_record_boundary = false;
    second.scan_bom = false;

    std::vector<SpeculativeParseChunk> chunks;
    chunks.push_back(first);
    chunks.push_back(second);

    std::vector<CSVRow> output;
    ParallelCSVParser parser(parse_flags, ws_flags, 2);
    const auto result = parser.parse_chunks(chunks, output);

    REQUIRE(result.chunks_processed == 2);
    REQUIRE(result.repair_count == 1);
    REQUIRE(output.size() == 3);
    REQUIRE(output[0][0] == "id");
    REQUIRE(output[1][0] == "1");
    REQUIRE(output[1][1] == "hello world");
    REQUIRE(output[1][2] == "ok");
    REQUIRE(output[2][0] == "2");
    REQUIRE(output[2][1] == "done");
    REQUIRE(output[2][2] == "ok");
}

TEST_CASE("ParallelCSVParser parses caller-owned chunks from the speculative scanner", "[raw_csv_parse][speculative][parallel]") {
    const auto parse_flags = internals::make_parse_flags(',', '"');
    const auto ws_flags = internals::WhitespaceMap();
    SpeculativeScanner scanner(parse_flags, 8);

    auto input = std::make_shared<std::string>(
        "id,text,status\n"
        "1,\"alpha\nbeta\",ok\n"
        "2,plain,ok\n"
        "3,\"comma,value\",ok\n"
    );

    auto chunks = make_speculative_parse_chunks(*input, input, 9, scanner);
    REQUIRE(chunks.size() > 3);

    std::vector<CSVRow> output;
    ParallelCSVParser parser(parse_flags, ws_flags, 3);
    const auto result = parser.parse_chunks(chunks, output);

    REQUIRE(result.chunks_processed == chunks.size());
    REQUIRE(output.size() == 4);
    REQUIRE(output[0][0] == "id");
    REQUIRE(output[1][0] == "1");
    REQUIRE(output[1][1] == "alpha\nbeta");
    REQUIRE(output[1][2] == "ok");
    REQUIRE(output[2][0] == "2");
    REQUIRE(output[2][1] == "plain");
    REQUIRE(output[2][2] == "ok");
    REQUIRE(output[3][0] == "3");
    REQUIRE(output[3][1] == "comma,value");
    REQUIRE(output[3][2] == "ok");
}

TEST_CASE("ParallelCSVParser can leave the final split row pending", "[raw_csv_parse][speculative][parallel]") {
    const auto parse_flags = internals::make_parse_flags(',', '"');
    const auto ws_flags = internals::WhitespaceMap();

    auto chunk0 = std::make_shared<std::string>("id,text,status\n1,\"alpha");
    SpeculativeParseChunk first;
    first.sequence_number = 0;
    first.offset = 0;
    first.bytes = *chunk0;
    first.owner = chunk0;
    first.speculation.sequence_number = 0;
    first.speculation.assumed_start_state = ParserDFAState(false);
    first.starts_at_record_boundary = true;
    first.scan_bom = true;

    auto chunk1 = std::make_shared<std::string>(" beta");
    SpeculativeParseChunk second;
    second.sequence_number = 1;
    second.offset = chunk0->size();
    second.bytes = *chunk1;
    second.owner = chunk1;
    second.speculation.sequence_number = 1;
    second.speculation.assumed_start_state = ParserDFAState(true);
    second.starts_at_record_boundary = false;
    second.scan_bom = false;

    std::vector<SpeculativeParseChunk> chunks;
    chunks.push_back(first);
    chunks.push_back(second);

    std::vector<CSVRow> output;
    ParallelCSVParser parser(parse_flags, ws_flags, 2);
    const auto result = parser.parse_chunks(chunks, output, false);

    REQUIRE(result.chunks_processed == 2);
    REQUIRE(result.repair_count == 0);
    REQUIRE(result.has_pending_suffix);
    REQUIRE(result.complete_prefix_length == std::string("id,text,status\n").size());
    REQUIRE(result.ending_state.quote_escape);
    REQUIRE(output.size() == 1);
    REQUIRE(output[0][0] == "id");
}

TEST_CASE("ParallelCSVParser repairs chunks split between CR and LF", "[raw_csv_parse][speculative][parallel]") {
    const auto parse_flags = internals::make_parse_flags(',', '"');
    const auto ws_flags = internals::WhitespaceMap();
    SpeculativeScanner scanner(parse_flags, 4);

    auto input = std::make_shared<std::string>("A,B\r\nC,D\r\n");
    auto chunks = make_speculative_parse_chunks(*input, input, 4, scanner);
    REQUIRE(chunks.size() == 3);

    std::vector<CSVRow> output;
    ParallelCSVParser parser(parse_flags, ws_flags, 2);
    const auto result = parser.parse_chunks(chunks, output);

    REQUIRE(result.chunks_processed == chunks.size());
    REQUIRE(result.repair_count >= 1);
    REQUIRE(output.size() == 2);
    REQUIRE(output[0][0] == "A");
    REQUIRE(output[0][1] == "B");
    REQUIRE(output[1][0] == "C");
    REQUIRE(output[1][1] == "D");
}

#if !defined(__EMSCRIPTEN__)
TEST_CASE("MmapParser speculative path preserves row order and split quoted rows", "[raw_csv_parse][speculative][mmap]") {
    FileGuard cleanup("./tests/data/tmp_speculative_mmap.csv");

    std::string content;
    size_t generated_rows = 0;
    while (content.size() < 500100) {
        content += std::to_string(generated_rows);
        content += ",prefix,row\n";
        generated_rows++;
    }

    content += "tail-quoted,\"alpha\n";
    content.append(internals::CSV_CHUNK_SIZE_FLOOR, 'x');
    content += "\nomega\",ok\n";
    content += "tail-plain,done,ok";

    {
        std::ofstream out(cleanup.filename, std::ios::binary);
        out << content;
    }

    CSVFormat format;
    format.no_header()
        .delimiter(',')
        .speculative_parallel()
        .speculative_parallel_min_bytes(1)
        .speculative_parallel_threads(2);

    RowCollection output;
    MmapParser parser(cleanup.filename, format);
    parser.set_output(output);

    while (!parser.eof()) {
        parser.next(internals::CSV_CHUNK_SIZE_FLOOR);
    }

    std::vector<CSVRow> rows;
    output.drain_front(rows, output.size());
    REQUIRE(rows.size() == generated_rows + 2);
    REQUIRE(rows[0][0] == "0");
    REQUIRE(rows[generated_rows - 1][0].get<std::string>() == std::to_string(generated_rows - 1));
    REQUIRE(rows[generated_rows][0] == "tail-quoted");
    REQUIRE(rows[generated_rows][1].get<std::string>() == "alpha\n" + std::string(internals::CSV_CHUNK_SIZE_FLOOR, 'x') + "\nomega");
    REQUIRE(rows[generated_rows][2] == "ok");
    REQUIRE(rows[generated_rows + 1][0] == "tail-plain");
    REQUIRE(rows[generated_rows + 1][1] == "done");
    REQUIRE(rows[generated_rows + 1][2] == "ok");
}
#endif

TEST_CASE("StreamParser speculative path preserves row order and split worker chunks", "[raw_csv_parse][speculative][stream]") {
    std::string content;
    size_t generated_rows = 0;
    while (content.size() < internals::CSV_CHUNK_SIZE_FLOOR - 32) {
        content += std::to_string(generated_rows);
        content += ",prefix,row\n";
        generated_rows++;
    }

    content += "tail-quoted,\"alpha\n";
    content.append(internals::CSV_CHUNK_SIZE_FLOOR / 2, 'x');
    content += "\nomega\",ok\n";
    content += "tail-plain,done,ok\n";

    std::stringstream input(content);
    CSVFormat format;
    format.no_header()
        .delimiter(',')
        .speculative_parallel()
        .speculative_parallel_min_bytes(1)
        .speculative_parallel_threads(2);

    RowCollection output;
    StreamParser<std::stringstream> parser(input, format);
    parser.set_output(output);

    while (!parser.eof()) {
        parser.next(internals::CSV_CHUNK_SIZE_FLOOR);
    }

    REQUIRE(parser.parse_worker_count() == 2);
    REQUIRE(parser.speculative_diagnostics().chunks > 0);
    std::vector<CSVRow> rows;
    output.drain_front(rows, output.size());
    REQUIRE(rows.size() == generated_rows + 2);
    REQUIRE(rows[0][0] == "0");
    REQUIRE(rows[generated_rows - 1][0].get<std::string>() == std::to_string(generated_rows - 1));
    REQUIRE(rows[generated_rows][0] == "tail-quoted");
    REQUIRE(rows[generated_rows][1].get<std::string>() == "alpha\n" + std::string(internals::CSV_CHUNK_SIZE_FLOOR / 2, 'x') + "\nomega");
    REQUIRE(rows[generated_rows][2] == "ok");
    REQUIRE(rows[generated_rows + 1][0] == "tail-plain");
    REQUIRE(rows[generated_rows + 1][1] == "done");
    REQUIRE(rows[generated_rows + 1][2] == "ok");
}

TEST_CASE("StreamParser speculative path carries quoted rows across buffered windows", "[raw_csv_parse][speculative][stream]") {
    std::string content = "first,ok,row\n";
    content += "big,\"";
    content.append((internals::CSV_CHUNK_SIZE_FLOOR * 2) + 128, 'q');
    content += "\",ok\n";
    content += "last,ok,row\n";

    std::stringstream input(content);
    CSVFormat format;
    format.no_header()
        .delimiter(',')
        .speculative_parallel()
        .speculative_parallel_min_bytes(1)
        .speculative_parallel_threads(2);

    RowCollection output;
    StreamParser<std::stringstream> parser(input, format);
    parser.set_output(output);

    while (!parser.eof()) {
        parser.next(internals::CSV_CHUNK_SIZE_FLOOR);
    }

    REQUIRE(parser.speculative_diagnostics().chunks > 0);
    std::vector<CSVRow> rows;
    output.drain_front(rows, output.size());
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0][0] == "first");
    REQUIRE(rows[1][0] == "big");
    REQUIRE(rows[1][1].get<std::string>() == std::string((internals::CSV_CHUNK_SIZE_FLOOR * 2) + 128, 'q'));
    REQUIRE(rows[1][2] == "ok");
    REQUIRE(rows[2][0] == "last");
}

TEST_CASE("StreamParser speculative path flushes pending suffix once at EOF", "[raw_csv_parse][speculative][stream]") {
    std::string content = "first,ok,row\n";
    content += "final,\"";
    content.append((internals::CSV_CHUNK_SIZE_FLOOR * 2) + 128, 'z');
    content += "\",done";

    std::stringstream input(content);
    CSVFormat format;
    format.no_header()
        .delimiter(',')
        .speculative_parallel()
        .speculative_parallel_min_bytes(1)
        .speculative_parallel_threads(2);

    RowCollection output;
    StreamParser<std::stringstream> parser(input, format);
    parser.set_output(output);

    while (!parser.eof()) {
        parser.next(internals::CSV_CHUNK_SIZE_FLOOR);
    }

    REQUIRE(parser.speculative_diagnostics().chunks > 0);
    std::vector<CSVRow> rows;
    output.drain_front(rows, output.size());
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0][0] == "first");
    REQUIRE(rows[1][0] == "final");
    REQUIRE(rows[1][1].get<std::string>() == std::string((internals::CSV_CHUNK_SIZE_FLOOR * 2) + 128, 'z'));
    REQUIRE(rows[1][2] == "done");
}
#endif

TEST_CASE("StreamParser stays serial when speculative parsing is disabled", "[raw_csv_parse][stream]") {
    std::stringstream input(
        "a,b,c\n"
        "1,2,3\n"
        "4,5,6\n"
    );
    CSVFormat format;
    format.no_header().delimiter(',');

    RowCollection output;
    StreamParser<std::stringstream> parser(input, format);
    parser.set_output(output);
    parser.next(internals::CSV_CHUNK_SIZE_FLOOR);

    REQUIRE(parser.parse_worker_count() == 1);
    REQUIRE(parser.speculative_diagnostics().chunks == 0);
    std::vector<CSVRow> rows;
    output.drain_front(rows, output.size());
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0][0] == "a");
    REQUIRE(rows[2][2] == "6");
}

#if !CSV_ENABLE_THREADS
TEST_CASE("StreamParser stays serial with speculative flag in no-thread builds", "[raw_csv_parse][stream]") {
    std::stringstream input(
        "a,b,c\n"
        "1,2,3\n"
        "4,5,6\n"
    );
    CSVFormat format;
    format.no_header()
        .delimiter(',')
        .speculative_parallel()
        .speculative_parallel_min_bytes(1)
        .speculative_parallel_threads(2);

    RowCollection output;
    StreamParser<std::stringstream> parser(input, format);
    parser.set_output(output);
    parser.next(internals::CSV_CHUNK_SIZE_FLOOR);

    REQUIRE(parser.parse_worker_count() == 1);
    REQUIRE(parser.speculative_diagnostics().chunks == 0);
    REQUIRE(output.size() == 3);
}
#endif
