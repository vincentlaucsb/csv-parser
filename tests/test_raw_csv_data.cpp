#include <catch2/catch_all.hpp>
#include "internal/basic_csv_parser.hpp"
#include "internal/csv_row.hpp"
#include "shared/file_guard.hpp"

#include <fstream>
#include <sstream>

using namespace csv;
using namespace csv::internals;
using RowCollectionTest = ThreadSafeDeque<CSVRow>;

TEST_CASE("Basic CSV Parse Test", "[raw_csv_parse]") {
    std::stringstream csv("A,B,C\r\n"
        "123,234,345\r\n"
        "1,2,3\r\n"
        "1,2,3");

    RowCollectionTest rows;

    StreamParser<std::stringstream> parser(
        csv,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    parser.set_output(rows);
    parser.next();

    auto row = rows.front();
    REQUIRE(row[0] == "A");
    REQUIRE(row[1] == "B");
    REQUIRE(row[2] == "C");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "123");
    REQUIRE(row[1] == "234");
    REQUIRE(row[2] == "345");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "2");
    REQUIRE(row[2] == "3");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "2");
    REQUIRE(row[2] == "3");
    REQUIRE(row.size() == 3);
}

TEST_CASE("Raw parser can emit rows into a vector sink", "[raw_csv_parse]") {
    std::stringstream csv(
        "A,B,C\n"
        "1,2,3\n"
        "4,5,6\n"
    );

    std::vector<CSVRow> parsed_rows;
    VectorRowSink sink(parsed_rows);

    StreamParser<std::stringstream> parser(
        csv,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    parser.set_output(sink);
    parser.next();

    REQUIRE(parsed_rows.size() == 3);
    REQUIRE(parsed_rows[0][0] == "A");
    REQUIRE(parsed_rows[1][1] == "2");
    REQUIRE(parsed_rows[2][2] == "6");
}

TEST_CASE("Raw parser can parse a caller-owned chunk directly", "[raw_csv_parse]") {
    std::stringstream unused_source;
    std::vector<CSVRow> parsed_rows;
    VectorRowSink sink(parsed_rows);

    StreamParser<std::stringstream> parser(
        unused_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    auto chunk = std::make_shared<std::string>(
        "left,right\n"
        "\"a,b\",2\n"
    );

    const auto result = parser.parse_chunk(*chunk, chunk, sink);

    REQUIRE(result.complete_prefix_length == chunk->size());
    REQUIRE_FALSE(result.ending_state.quote_escape);
    REQUIRE_FALSE(result.ending_state.pending_quote);
    REQUIRE(parsed_rows.size() == 2);
    REQUIRE(parsed_rows[0][0] == "left");
    REQUIRE(parsed_rows[1][0] == "a,b");
    REQUIRE(parsed_rows[1][1] == "2");
}

TEST_CASE("CSVRow raw_str uses record boundaries rather than newline search", "[raw_csv_parse]") {
    std::stringstream unused_source;
    std::vector<CSVRow> parsed_rows;
    VectorRowSink sink(parsed_rows);

    StreamParser<std::stringstream> parser(
        unused_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    auto chunk = std::make_shared<std::string>(
        "id,text,status\r\n"
        "1,\"hello\nworld\",ok\r\n"
        "2,plain,ok\r\n"
    );

    const auto result = parser.parse_chunk(*chunk, chunk, sink);

    REQUIRE(result.complete_prefix_length == chunk->size());
    REQUIRE(parsed_rows.size() == 3);
    REQUIRE(parsed_rows[0].raw_str() == "id,text,status");
    REQUIRE(parsed_rows[1].raw_str() == "1,\"hello\nworld\",ok");
    REQUIRE(parsed_rows[2].raw_str() == "2,plain,ok");
}

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

TEST_CASE("Speculative scanner uses probability model for unresolved quote-boundary ties", "[raw_csv_parse][speculative]") {
    SpeculativeScanner scanner(internals::make_parse_flags(',', '"'));
    const std::string chunk =
        "\",a\",b\n"
        "next,row\n";

    const auto speculation = scanner.speculate(6, 32768, chunk);

    REQUIRE(speculation.ambiguous);
    REQUIRE(speculation.used_probability_model);
    REQUIRE_FALSE(speculation.used_record_size_heuristic);
    REQUIRE(speculation.quoted_start_odds > 1);
    REQUIRE(speculation.assumed_start_state.quote_escape);
}

TEST_CASE("Parsed chunk rows split edge fragments from complete rows", "[raw_csv_parse][fragments]") {
    std::stringstream unused_source;
    StreamParser<std::stringstream> parser(
        unused_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    SECTION("record-boundary chunk keeps trailing partial row as suffix") {
        std::vector<CSVRow> parsed_rows;
        VectorRowSink sink(parsed_rows);
        auto chunk = std::make_shared<std::string>("id,value\n1,\"long");

        const auto parse_result = parser.parse_chunk(*chunk, chunk, sink);
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
        VectorRowSink sink(parsed_rows);
        auto chunk = std::make_shared<std::string>(" value\",2\n3,Bob\n4,\"tail");
        ParserChunkOptions options(ParserDFAState(true), false);

        const auto parse_result = parser.parse_chunk(*chunk, chunk, sink, options);
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
        VectorRowSink sink(parsed_rows);
        auto chunk = std::make_shared<std::string>("line one\nline two\",tail\nnext,row\npartial");
        ParserChunkOptions options(ParserDFAState(true), false);

        const auto parse_result = parser.parse_chunk(*chunk, chunk, sink, options);
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
        VectorRowSink sink(parsed_rows);
        auto chunk = std::make_shared<std::string>(" still inside the same quoted field");
        ParserChunkOptions options(ParserDFAState(true), false);

        const auto parse_result = parser.parse_chunk(*chunk, chunk, sink, options);
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

TEST_CASE("Speculative validator repairs wrongly seeded continuation chunks", "[raw_csv_parse][speculative][validator]") {
    std::stringstream chunk0_source;
    std::stringstream chunk1_source;
    std::stringstream repair_source;

    StreamParser<std::stringstream> chunk0_parser(
        chunk0_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    StreamParser<std::stringstream> chunk1_parser(
        chunk1_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    StreamParser<std::stringstream> repair_parser(
        repair_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    auto chunk0 = std::make_shared<std::string>("id,text,status\n1,\"hello ");
    std::vector<CSVRow> parsed0;
    VectorRowSink sink0(parsed0);
    const auto result0 = chunk0_parser.parse_chunk(*chunk0, chunk0, sink0);
    auto rows0 = split_parsed_chunk_rows(0, *chunk0, chunk0, result0, std::move(parsed0), true);

    auto chunk1 = std::make_shared<std::string>("world\",ok\n2,done,ok\n");
    std::vector<CSVRow> parsed1_wrong;
    VectorRowSink sink1(parsed1_wrong);
    const auto result1_wrong = chunk1_parser.parse_chunk(
        *chunk1,
        chunk1,
        sink1,
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
    VectorRowSink output_sink(output);
    SpeculativeParseValidator validator(repair_parser, output_sink);

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

TEST_CASE("Speculative validator carries split rows across chunks without record boundaries", "[raw_csv_parse][speculative][validator]") {
    std::stringstream chunk0_source;
    std::stringstream chunk1_source;
    std::stringstream chunk2_source;
    std::stringstream repair_source;

    StreamParser<std::stringstream> chunk0_parser(
        chunk0_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    StreamParser<std::stringstream> chunk1_parser(
        chunk1_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    StreamParser<std::stringstream> chunk2_parser(
        chunk2_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );
    StreamParser<std::stringstream> repair_parser(
        repair_source,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    auto chunk0 = std::make_shared<std::string>("id,text,status\n1,\"alpha");
    std::vector<CSVRow> parsed0;
    VectorRowSink sink0(parsed0);
    const auto result0 = chunk0_parser.parse_chunk(*chunk0, chunk0, sink0);
    auto rows0 = split_parsed_chunk_rows(0, *chunk0, chunk0, result0, std::move(parsed0), true);

    auto chunk1 = std::make_shared<std::string>(" beta");
    std::vector<CSVRow> parsed1;
    VectorRowSink sink1(parsed1);
    const auto result1 = chunk1_parser.parse_chunk(
        *chunk1,
        chunk1,
        sink1,
        ParserChunkOptions(ParserDFAState(true), false)
    );
    auto rows1 = split_parsed_chunk_rows(1, *chunk1, chunk1, result1, std::move(parsed1), false);

    auto chunk2 = std::make_shared<std::string>(" gamma\",ok\n");
    std::vector<CSVRow> parsed2;
    VectorRowSink sink2(parsed2);
    const auto result2 = chunk2_parser.parse_chunk(
        *chunk2,
        chunk2,
        sink2,
        ParserChunkOptions(ParserDFAState(true), false)
    );
    auto rows2 = split_parsed_chunk_rows(2, *chunk2, chunk2, result2, std::move(parsed2), false);

    std::vector<CSVRow> output;
    VectorRowSink output_sink(output);
    SpeculativeParseValidator validator(repair_parser, output_sink);

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
    VectorRowSink output_sink(output);
    ParallelCSVParser parser(parse_flags, ws_flags, 2);
    const auto result = parser.parse_chunks(chunks, output_sink);

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
    VectorRowSink output_sink(output);
    ParallelCSVParser parser(parse_flags, ws_flags, 3);
    const auto result = parser.parse_chunks(chunks, output_sink);

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
    VectorRowSink output_sink(output);
    ParallelCSVParser parser(parse_flags, ws_flags, 2);
    const auto result = parser.parse_chunks(chunks, output_sink, false);

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
    VectorRowSink output_sink(output);
    ParallelCSVParser parser(parse_flags, ws_flags, 2);
    const auto result = parser.parse_chunks(chunks, output_sink);

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

    std::vector<CSVRow> output;
    VectorRowSink output_sink(output);
    MmapParser parser(cleanup.filename, format);
    parser.set_output(output_sink);

    while (!parser.eof()) {
        parser.next(internals::CSV_CHUNK_SIZE_FLOOR);
    }

    REQUIRE(output.size() == generated_rows + 2);
    REQUIRE(output[0][0] == "0");
    REQUIRE(output[generated_rows - 1][0].get<std::string>() == std::to_string(generated_rows - 1));
    REQUIRE(output[generated_rows][0] == "tail-quoted");
    REQUIRE(output[generated_rows][1].get<std::string>() == "alpha\n" + std::string(internals::CSV_CHUNK_SIZE_FLOOR, 'x') + "\nomega");
    REQUIRE(output[generated_rows][2] == "ok");
    REQUIRE(output[generated_rows + 1][0] == "tail-plain");
    REQUIRE(output[generated_rows + 1][1] == "done");
    REQUIRE(output[generated_rows + 1][2] == "ok");
}
#endif

TEST_CASE("Test Quote Escapes", "[test_parse_quote_escape]") {
    std::stringstream csv(""
        "\"A\",\"B\",\"C\"\r\n"   // Quoted fields w/ no escapes
        "123,\"234,345\",456\r\n" // Escaped comma
        "1,\"2\"\"3\",4\r\n"      // Escaped quote
        "1,\"23\"\"34\",5\r\n"      // Another escaped quote
        "1,\"\",2\r\n");           // Empty Field

    RowCollectionTest rows;

    StreamParser<std::stringstream> parser(
        csv,
        internals::make_parse_flags(',', '"'),
        internals::WhitespaceMap()
    );

    parser.set_output(rows);
    parser.next();

    auto row = rows.front();
    REQUIRE(row[0] == "A");
    REQUIRE(row[1] == "B");
    REQUIRE(row[2] == "C");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "123");
    REQUIRE(row[1] == "234,345");
    REQUIRE(row[2] == "456");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "2\"3");
    REQUIRE(row[2] == "4");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "23\"34");
    REQUIRE(row[2] == "5");
    REQUIRE(row.size() == 3);

    rows.pop_front();
    row = rows.front();
    REQUIRE(row[0] == "1");
    REQUIRE(row[1] == "");
    REQUIRE(row[2] == "2");
    REQUIRE(row.size() == 3);
}

TEST_CASE("Parser DFA state can be seeded and reported", "[raw_csv_parse][dfa_state]") {
    SECTION("Reports unfinished quoted field at chunk end") {
        std::stringstream csv("A,\"unfinished");
        RowCollectionTest rows;

        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::WhitespaceMap()
        );

        parser.set_output(rows);
        parser.next();

        const auto state = parser.ending_state();
        REQUIRE(state.quote_escape);
        REQUIRE_FALSE(state.pending_quote);
    }

    SECTION("Reports pending quote when chunk ends on quoted-field quote") {
        std::stringstream csv("\"abc\"z\n");
        RowCollectionTest rows;

        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::WhitespaceMap()
        );

        parser.set_output(rows);
        parser.next(5);

        const auto state = parser.ending_state();
        REQUIRE(state.quote_escape);
        REQUIRE(state.pending_quote);
        REQUIRE(rows.empty());
    }

    SECTION("Seeded quoted state treats delimiters and newlines as field content") {
        std::stringstream csv("alpha\nbeta\",tail\n");
        RowCollectionTest rows;

        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::WhitespaceMap()
        );

        parser.set_output(rows);
        parser.reset_with_initial_state(true);
        parser.next();

        REQUIRE_FALSE(parser.ending_state().quote_escape);
        REQUIRE_FALSE(parser.ending_state().pending_quote);
        REQUIRE(rows.size() == 1);

        const auto row = rows.front();
        REQUIRE(row.size() == 2);
        REQUIRE(row[0] == "alpha\nbeta");
        REQUIRE(row[1] == "tail");
    }
}

inline std::vector<std::string> make_whitespace_test_cases() {
    std::vector<std::string> test_cases = {};
    std::stringstream ss;

    ss << "1, two,3" << std::endl
        << "4, ,5" << std::endl
        << " ,6, " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    // Lots of Whitespace
    ss << "1, two,3" << std::endl
        << "4,                    ,5" << std::endl
        << "         ,6,       " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    // Same as above but there's whitespace around 6
    ss << "1, two,3" << std::endl
        << "4,                    ,5" << std::endl
        << "         , 6 ,       " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    // Tabs
    ss << "1, two,3" << std::endl
        << "4, \t ,5" << std::endl
        << "\t\t\t\t\t ,6, \t " << std::endl
        << "7,8,9 " << std::endl;
    test_cases.push_back(ss.str());
    ss.clear();

    return test_cases;
}

TEST_CASE("Test Parser Whitespace Trimming", "[test_csv_trim]") {
    auto row_str = GENERATE(as<std::string> {},
        "A,B,C\r\n" // Header row
        "123,\"234\n,345\",456\r\n",

        // Random spaces
        "A,B,C\r\n"
        "   123,\"234\n,345\",    456\r\n",

        // Random spaces + tabs
        "A,B,C\r\n"
        "\t\t   123,\"234\n,345\",    456\r\n",

        // Spaces in quote escaped field
        "A,B,C\r\n"
        "\t\t   123,\"   234\n,345  \t\",    456\r\n",

        // Spaces in one header column
        "A,B,        C\r\n"
        "123,\"234\n,345\",456\r\n",

        // Random spaces + tabs in header
        "\t A,  B\t,     C\r\n"
        "123,\"234\n,345\",456\r\n",

        // Random spaces in header + data
        "A,B,        C\r\n"
        "123,\"234\n,345\",  456\r\n"
    );

    SECTION("Parse Test") {
        using namespace std;

        RowCollectionTest rows;

        auto csv = std::stringstream(row_str);
        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::make_ws_flags({ ' ', '\t' })
        );

        parser.set_output(rows);
        parser.next();

        auto header = rows[0];
        REQUIRE(vector<string>(header) == vector<string>(
            { "A", "B", "C" }));

        auto row = rows[1];
        REQUIRE(vector<string>(row) ==
            vector<string>({ "123", "234\n,345", "456" }));
        REQUIRE(row[0] == "123");
        REQUIRE(row[1] == "234\n,345");
        REQUIRE(row[2] == "456");
    }
}

TEST_CASE("Test Parser Whitespace Trimming w/ Empty Fields", "[test_raw_ws_trim]") {
    auto csv_string = GENERATE(from_range(make_whitespace_test_cases()));

    SECTION("Parse Test") {
        RowCollectionTest rows;

        auto csv = std::stringstream(csv_string);
        StreamParser<std::stringstream> parser(
            csv,
            internals::make_parse_flags(',', '"'),
            internals::make_ws_flags({ ' ', '\t' })
        );

        parser.set_output(rows);

        parser.next();

        size_t row_no = 0;
        for (auto& row : rows) {
            switch (row_no) {
            case 0:
                REQUIRE(row[0].get<uint32_t>() == 1);
                REQUIRE(row[1].get<std::string>() == "two");
                REQUIRE(row[2].get<uint32_t>() == 3);
                break;

            case 1:
                REQUIRE(row[0].get<uint32_t>() == 4);
                REQUIRE(row[1].is_null());
                REQUIRE(row[2].get<uint32_t>() == 5);
                break;

            case 2:
                REQUIRE(row[0].is_null());
                REQUIRE(row[1].get<uint32_t>() == 6);
                REQUIRE(row[2].is_null());
                break;

            case 3:
                REQUIRE(row[0].get<uint32_t>() == 7);
                REQUIRE(row[1].get<uint32_t>() == 8);
                REQUIRE(row[2].get<uint32_t>() == 9);
                break;
            }

            row_no++;
        }
    }
}
