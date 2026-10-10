#include <cstdint>
#include <atomic>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>

#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/generated_file.hpp"
#include "shared/timeout_helper.hpp"

using namespace csv;

namespace {
    const std::string& issue_333_file() {
        static csv_test::GeneratedFile file("csv_parser_issue_333.csv");
        return file.path([](std::ofstream& out) {
            out << "id,name,value\n";
            for (size_t i = 1; i <= 500001; ++i) {
                out << i << ",person_" << i << ',' << i * 7 << '\n';
            }
        });
    }
}

TEST_CASE("DataFrame: const row proxies observe first and subsequent edits - issue 333",
    "[data_frame][issue_333]") {
    const auto& filename = issue_333_file();
    auto validate = [](CSVReader& reader) {
        DataFrame<> frame(reader);
        REQUIRE(frame.size() == 500001);
        const auto& source = frame;
        auto first = source.at(0);
        const auto last = source.at(500000);
        auto names = source.column_view("name");
        auto pending = frame.at(100)["name"];
        auto copied = pending;
        DataFrameCell assigned;
        assigned = pending;
        DataFrameCell moved(std::move(pending));

        // These proxies predate both the dirty transition and their row overlays.
        frame.at(0)["name"] = "first edit";
        frame.at(500000)["name"] = "last edit";
        CHECK(first["name"].get<std::string>() == "first edit");
        CHECK(last["name"].get<std::string>() == "last edit");
        CHECK(names[0].get<std::string>() == "first edit");
        CHECK(names.get_sv(500000) == "last edit");
        copied = "copied cell";
        assigned = "assigned cell";
        moved = "moved cell";
        CHECK(source.at(100)["name"].get<std::string>() == "moved cell");

        auto after = source.at(0);
        frame.at(0)["value"] = "changed value";
        CHECK(first["value"].get<std::string>() == "changed value");
        CHECK(after["value"].get<std::string>() == "changed value");
        CHECK(std::vector<std::string>(first) ==
            std::vector<std::string>{"1", "first edit", "changed value"});
        CHECK(first.to_json() == "{\"id\":1,\"name\":\"first edit\",\"value\":\"changed value\"}");
        CHECK(first.get_underlying_row()["name"].get<std::string>() == "person_1");
        CHECK_THROWS_AS(first["name"] = "blocked", std::runtime_error);
        DataFrame<> moved_frame(std::move(frame));
        moved_frame.at(500000)["value"] = "after frame move";
        CHECK(moved_frame.at(500000)["value"].get<std::string>() == "after frame move");
        DataFrame<> assigned_frame;
        assigned_frame = std::move(moved_frame);
        assigned_frame.at(0)["value"] = "after frame assignment";
        CHECK(assigned_frame.at(0)["value"].get<std::string>() == "after frame assignment");
    };
    SECTION("mmap") {
        CSVReader reader(filename, CSVFormat());
        validate(reader);
    }
    SECTION("stream") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, CSVFormat());
        validate(reader);
    }
}

TEST_CASE("DataFrame: column rebuild preserves established typed keys - issue 333",
    "[data_frame][issue_333]") {
    const auto& filename = issue_333_file();
    const bool custom_keys = GENERATE(false, true);
    auto validate = [custom_keys](CSVReader& reader) {
        DataFrame<int> frame = custom_keys
            ? DataFrame<int>(reader, [](const CSVRow& row) { return row["id"].get<int>(); })
            : DataFrame<int>(reader, "id");
        REQUIRE(frame.size() == 500001);
        // Duplicate and unconvertible edited values must not redefine row identity.
        frame.at(0)["id"] = "2";
        frame.at(1)["id"] = "not an integer";
        frame.at(500000)["name"] = "line\rbreak\n\"quoted\",tail";
        REQUIRE(frame.contains(1));
        REQUIRE(frame.contains(2));
        REQUIRE_NOTHROW(frame.insert_column(0, "extra", "constant"));
        REQUIRE(frame.size() == 500001);
        CHECK(frame.at(0).key() == 1);
        CHECK(frame.at(1).key() == 2);
        CHECK(frame[1]["id"].get<std::string>() == "2");
        CHECK(frame[2]["id"].get<std::string>() == "not an integer");
        CHECK(frame[500001]["name"].get<std::string>() == "line\rbreak\n\"quoted\",tail");
        CHECK_THROWS_AS(frame.insert_row(0, {"constant", "1", "duplicate", "0"}), std::runtime_error);
        REQUIRE(frame.column_view("name").erase());
        REQUIRE_NOTHROW(frame.append_column("tail"));
        CHECK(frame[1]["id"].get<std::string>() == "2");
        CHECK(frame[2]["value"].get<int>() == 14);
        CHECK(frame[500001]["value"].get<int>() == 3500007);
        if (!custom_keys) {
            CHECK_THROWS_AS(frame.column_view("id").erase(), std::runtime_error);
        }
    };
    SECTION("mmap") {
        CSVReader reader(filename, CSVFormat());
        validate(reader);
    }
    SECTION("stream") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, CSVFormat());
        validate(reader);
    }
}

TEST_CASE("DataFrame: empty column insertion retains zero-column rows - issue 333",
    "[data_frame][issue_333]") {
    DataFrame<> frame;
    frame.insert_row(0, {});
    frame.insert_row(1, {});
    REQUIRE(frame.size() == 2);
    std::string default_value;
    SECTION("empty default") {}
    SECTION("nonempty default") { default_value = "present"; }
    frame.append_column("empty", default_value);
    REQUIRE(frame.size() == 2);
    REQUIRE(frame.n_cols() == 1);
    CHECK(std::vector<std::string>(frame.at(0)) == std::vector<std::string>{default_value});
    CHECK(std::vector<std::string>(frame.at(1)) == std::vector<std::string>{default_value});
    REQUIRE(frame.column_view(0).erase());
    REQUIRE(frame.n_cols() == 0);
    frame.append_column("again");
    REQUIRE(frame.size() == 2);
    CHECK(frame.at(1)[0].get<std::string>().empty());
}

TEST_CASE("DataFrame: selection snapshots edits and hidden columns independently",
    "[data_frame][issue_333]") {
    const auto& filename = issue_333_file();
    auto validate = [](CSVReader& reader) {
        DataFrame<> frame(reader);
        frame.at(500000)["name"] = "snapshot";
        REQUIRE(frame.column_view("value").erase());
        std::vector<std::uint8_t> mask(frame.size(), 0);
        mask[500000] = 1;
        auto selected = frame.selected_rows(mask);
        REQUIRE(selected.size() == 1);
        CHECK(selected.columns() == std::vector<std::string>{"id", "name"});
        CHECK(selected.at(0)["name"].get<std::string>() == "snapshot");
        frame.at(500000)["name"] = "source changed";
        CHECK(selected.at(0)["name"].get<std::string>() == "snapshot");
        selected.at(0)["name"] = "selection changed";
        CHECK(frame.at(500000)["name"].get<std::string>() == "source changed");
        selected.append_column("extra", "x");
        CHECK(selected.at(0)["name"].get<std::string>() == "selection changed");
        CHECK(selected.n_cols() == 3);
        CHECK_FALSE(selected.has_column("value"));
        auto empty = frame.selected_rows(std::vector<std::uint8_t>(frame.size(), 0));
        CHECK(empty.empty());
        CHECK(empty.columns() == frame.columns());
    };
    SECTION("mmap") {
        CSVReader reader(filename, CSVFormat());
        validate(reader);
    }
    SECTION("stream") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, CSVFormat());
        validate(reader);
    }
}

TEST_CASE("DataFrame: selected rows preserve independent sparse edits", "[data_frame][selected_rows]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
        "3,Carol,30\n"
        "4,David,40\n"
    );
    CSVReader reader(input);
    DataFrame<> frame;
    SECTION("Unkeyed source") { frame = DataFrame<>(reader); }
    SECTION("Keyed source") { frame = DataFrame<>(reader, "id"); }

    frame.at(0)["name"] = "Excluded";
    frame.at(1)["name"] = "Robert";
    frame.at(1)["value"] = "95";
    frame.at(3)["value"] = "45";

    const auto& source = frame;
    auto selected = source.selected_rows({0, 1, 0, 2});
    REQUIRE(selected.size() == 2);
    REQUIRE(selected.columns() == frame.columns());
    REQUIRE_THROWS_AS(selected["2"], std::runtime_error);
    REQUIRE(std::vector<std::string>(selected.at(0)) == std::vector<std::string>{"2", "Robert", "95"});
    REQUIRE(std::vector<std::string>(selected.at(1)) == std::vector<std::string>{"4", "David", "45"});
    REQUIRE(selected.column("value") == std::vector<std::string>{"95", "45"});
    REQUIRE(selected.column_view(1)[0].get<std::string>() == "Robert");
    REQUIRE(selected.at(0).to_json() == "{\"id\":2,\"name\":\"Robert\",\"value\":95}");
    REQUIRE(selected.at(0).to_json_array() == "[2,\"Robert\",95]");

    // Selection should still share parsed bytes, not materialize edited rows.
    REQUIRE(selected.at(0).get_underlying_row()["name"].get_sv().data()
        == source.at(1).get_underlying_row()["name"].get_sv().data());

    std::stringstream output;
    auto writer = make_csv_writer(output);
    writer << selected.columns();
    for (const auto& row : selected) {
#ifdef CSV_HAS_CXX20
        writer << row;
#else
        writer << std::vector<std::string>(row);
#endif
    }
    REQUIRE(output.str() == "id,name,value\n2,Robert,95\n4,David,45\n");

    const auto& const_selected = selected;
    REQUIRE_THROWS_AS(const_selected.at(0)["name"] = "Blocked", std::runtime_error);
    REQUIRE_THROWS_AS(const_selected.column_view("name")[0] = "Blocked", std::runtime_error);

    // Edits on either frame must not leak into the other frame's overlay.
    frame.at(1)["name"] = "Source only";
    REQUIRE(selected.at(0)["name"].get<std::string>() == "Robert");
    selected.at(0)["value"] = "100";
    REQUIRE(frame.at(1)["value"].get<std::string>() == "95");

    auto repeated = selected.selected_rows({1, 0});
    REQUIRE(std::vector<std::string>(repeated.at(0)) == std::vector<std::string>{"2", "Robert", "100"});
    selected.at(0)["name"] = "Selected only";
    REQUIRE(repeated.at(0)["name"].get<std::string>() == "Robert");
}

TEST_CASE("DataFrame: selected rows preserve physical edit indices after structural changes", "[data_frame][selected_rows]") {
    std::istringstream input("id,name,value\n1,Alice,10\n2,Bob,20\n3,Carol,30\n");
    CSVFormat format;
    format.column_names_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    CSVReader reader(input, format);
    DataFrame<> frame(reader, "id");
    frame.at(0)["name"] = "Hidden";
    frame.at(1)["value"] = "95";
    frame.at(2)["value"] = "";
    REQUIRE(frame.column_view("name").erase());
    REQUIRE(frame.at(0).erase());
    frame.insert_row(0, {"4", "40"});

    auto selected = frame.selected_rows({0, 1, 1});
    REQUIRE(selected.columns() == std::vector<std::string>{"id", "value"});
    REQUIRE(selected.index_of("VALUE") == 1);
    REQUIRE_FALSE(selected.has_column("name"));
    REQUIRE(std::vector<std::string>(selected.at(0)) == std::vector<std::string>{"2", "95"});
    REQUIRE(std::vector<std::string>(selected.at(1)) == std::vector<std::string>{"3", ""});
    REQUIRE(selected.column_view(1)[0].get<std::string>() == "95");
    REQUIRE(selected.at(0).to_json() == "{\"id\":2,\"value\":95}");
    REQUIRE(selected.at(1)["VALUE"].get<std::string>().empty());

    std::stringstream output;
    auto writer = make_csv_writer(output);
    writer << selected.columns();
    for (const auto& row : selected) {
#ifdef CSV_HAS_CXX20
        writer << row;
#else
        writer << std::vector<std::string>(row);
#endif
    }
    REQUIRE(output.str() == "id,value\n2,95\n3,\n");

    auto repeated = selected.selected_rows({0, 1});
    REQUIRE(std::vector<std::string>(repeated.at(0)) == std::vector<std::string>{"3", ""});
}

TEST_CASE("DataFrame: materialization owns preserved view keys after reader destruction",
    "[data_frame][view_keys]") {
    const auto& filename = issue_333_file();
    const bool custom_keys = GENERATE(false, true);
    DataFrame<csv::string_view> frame;
    auto load = [custom_keys, &frame](CSVReader& reader) {
        frame = custom_keys
            ? DataFrame<csv::string_view>(reader, [](const CSVRow& row) { return row["id"].get<csv::string_view>(); })
            : DataFrame<csv::string_view>(reader, "id");
    };
    SECTION("mmap") {
        CSVReader reader(filename, CSVFormat());
        load(reader);
    }
    SECTION("stream") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, CSVFormat());
        load(reader);
    }

    // Neither parser nor input remains alive. Build the cached index before the
    // original backing rows are released; it also contains borrowed key views.
    REQUIRE(frame.size() == 500001);
    REQUIRE(frame.contains("1"));
    frame.at(0)["id"] = "edited key value";
    frame.append_column("extra", "x");
    REQUIRE(frame.at(0).key() == csv::string_view("1"));
    REQUIRE(frame.contains("1"));
    REQUIRE(frame["1"]["id"].get<std::string>() == "edited key value");
    REQUIRE(frame.contains("500001"));

    if (!custom_keys) {
        frame.insert_row(1, {"inserted", "new row", "99", "x"});
    }
    frame.append_column("again");
    REQUIRE(frame.contains("1"));
    REQUIRE(frame.at(0).key() == csv::string_view("1"));
    if (!custom_keys) {
        REQUIRE(frame["inserted"]["value"].get<int>() == 99);
    }
    DataFrame<csv::string_view> moved(std::move(frame));
    moved.append_column("after_move");
    REQUIRE(moved.contains("500001"));
    REQUIRE(moved["1"]["id"].get<std::string>() == "edited key value");

    // Compact the preserved keys to fewer bytes than a string's inline buffer.
    // Moving their owner must also preserve these small-string-backed views.
    while (moved.size() > 2) {
        moved.at(moved.size() - 1).erase();
    }
    moved.append_column("small_keys");
    DataFrame<csv::string_view> small(std::move(moved));
    REQUIRE(small.contains("1"));
    REQUIRE(small.at(0).key() == csv::string_view("1"));
    small.at(1).erase();
    small.at(0).erase();
    small.append_column("no_keys");
    REQUIRE(small.empty());
    REQUIRE_FALSE(small.contains("1"));
}

TEST_CASE("DataFrame: standalone mutable proxies retain edits and access permissions",
    "[data_frame][coverage]") {
    // A row built in memory isolates proxy binding from the parser paths.
    DataFrame<> source;
    source.append_column("name");
    source.append_column("value");
    source.insert_row(0, {"original", "22"});
    const auto source_row = source.at(0);
    const auto& raw = source_row.get_underlying_row();
    RowOverlay overlay;
    DataFrameRow<std::string> detached(&raw, static_cast<DataFrame<>*>(nullptr), 0, &overlay, nullptr);

    auto cell = detached["name"];
    cell = "standalone edited value";
    REQUIRE(detached["name"].get<std::string>() == "standalone edited value");
    REQUIRE(raw["name"].get<std::string>() == "original");
    auto copied = cell;
    DataFrameCell moved(std::move(cell));
    overlay.set(0, "later overlay edit");
    REQUIRE(copied.get<std::string>() == "standalone edited value");
    REQUIRE(moved.get<std::string>() == "standalone edited value");
    REQUIRE(detached["name"].get<std::string>() == "later overlay edit");

    const DataFrameCell read_only(&raw, static_cast<const RowOverlay*>(&overlay), 0);
    moved = read_only;
    REQUIRE(moved.get<std::string>() == "later overlay edit");
    REQUIRE_THROWS_AS(moved = "blocked", std::runtime_error);
    moved = detached["value"];
    REQUIRE(moved.get<int>() == 22);
    moved = "33";
    REQUIRE(detached["value"].get<int>() == 33);

    DataFrameCell empty;
    DataFrameCell empty_copy(empty);
    DataFrameCell empty_move(std::move(empty_copy));
    REQUIRE(empty_move.is_null());
    copied = empty;
    REQUIRE(copied.get_sv().empty());
    moved = std::move(empty_move);
    REQUIRE(moved.is_null());
    REQUIRE_THROWS_AS(moved = "unbound", std::runtime_error);

    DataFrameCell no_overlay(&raw, static_cast<RowOverlay*>(nullptr), 0);
    REQUIRE(no_overlay.get<std::string>() == "original");
    REQUIRE_THROWS_AS(no_overlay = "unbound", std::runtime_error);
}

TEST_CASE("DataFrame: cell position bounds remain enforced after edits",
    "[data_frame][coverage]") {
    DataFrame<> frame;
    frame.append_column("name");
    frame.insert_row(0, {"original"});
    SECTION("clean") {}
    SECTION("dirty") { frame.at(0)["name"] = "edited"; }
    REQUIRE_THROWS_AS(frame.at(0)[frame.n_cols()], std::out_of_range);
    const auto& source = frame;
    REQUIRE_THROWS_AS(source.at(0)[source.n_cols()], std::out_of_range);
    REQUIRE_THROWS_AS(source.column_view("name").get_sv(source.size()), std::out_of_range);
}

TEST_CASE("DataFrame: row insertion with identity mapping shifts sparse edits",
    "[data_frame][coverage]") {
    const auto& filename = issue_333_file();
    const bool keyed = GENERATE(false, true);
    auto validate = [keyed](CSVReader& reader) {
        DataFrame<> frame = keyed ? DataFrame<>(reader, "id") : DataFrame<>(reader);
        REQUIRE(frame.size() == 500001);
        if (keyed) { REQUIRE(frame.contains("1")); }
        frame.at(0)["name"] = "edited first";
        frame.at(500000)["name"] = "edited last";
        // No column has been deleted: dirty insertion uses the identity mapping.
        frame.insert_row(0, {"new-row", "inserted", "17"});
        REQUIRE(frame.size() == 500002);
        REQUIRE(frame.at(1)["name"].get<std::string>() == "edited first");
        REQUIRE(frame.at(500001)["name"].get<std::string>() == "edited last");
        frame.at(0)["name"] = "edited inserted";
        REQUIRE(frame.at(0).to_json() == "{\"id\":\"new-row\",\"name\":\"edited inserted\",\"value\":17}");
        frame.append_column("extra", "default");
        REQUIRE(frame.at(0)["name"].get<std::string>() == "edited inserted");
        REQUIRE(frame.at(1)["name"].get<std::string>() == "edited first");
        REQUIRE(frame.at(500001)["value"].get<int>() == 3500007);
        REQUIRE(frame.at(500001)["extra"].get<std::string>() == "default");
        if (keyed) {
            REQUIRE(frame["new-row"]["name"].get<std::string>() == "edited inserted");
            REQUIRE(frame["1"]["name"].get<std::string>() == "edited first");
            REQUIRE(frame["500001"]["name"].get<std::string>() == "edited last");
        }
    };
    SECTION("mmap") {
        CSVReader reader(filename, CSVFormat());
        validate(reader);
    }
    SECTION("stream") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, CSVFormat());
        validate(reader);
    }
}

TEST_CASE("DataFrame: materialization preserves an empty borrowed key",
    "[data_frame][coverage][view_keys]") {
    const auto& filename = issue_333_file();
    DataFrame<csv::string_view> frame;
    auto load = [&frame](CSVReader& reader) {
        frame = DataFrame<csv::string_view>(reader, [](const CSVRow& row) -> csv::string_view {
            const auto key = row["id"].get<csv::string_view>();
            return key == csv::string_view("1") ? csv::string_view() : key;
        });
    };
    SECTION("mmap") {
        CSVReader reader(filename, CSVFormat());
        load(reader);
    }
    SECTION("stream") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, CSVFormat());
        load(reader);
    }
    REQUIRE(frame.size() == 500001);
    REQUIRE(frame.contains(csv::string_view()));
    frame.at(0)["name"] = "empty key row";
    frame.append_column("extra");
    REQUIRE(frame.at(0).key().empty());
    REQUIRE(frame.contains(csv::string_view()));
    REQUIRE(frame[csv::string_view()]["name"].get<std::string>() == "empty key row");
    REQUIRE(frame.contains("500001"));
    frame.insert_column(0, "prefix", "x");
    REQUIRE(frame[csv::string_view()]["prefix"].get<std::string>() == "x");
    REQUIRE(frame[csv::string_view()]["name"].get<std::string>() == "empty key row");
}

TEST_CASE("DataFrame: repeated backend transitions preserve schema and proxy reads",
    "[data_frame][backend]") {
    DataFrame<> frame;
    frame.append_column("id");
    frame.append_column("value");
    frame.insert_row(0, {"1", "original"});
    for (size_t i = 0; i < 20; ++i) {
        const auto& source = frame;
        const auto row = source.at(0);
        auto column = source.column_view("value");
        auto pending = frame.at(0)["value"];
        const std::string edited = "edit_" + std::to_string(i);
        pending = edited;
        REQUIRE(row["value"].get<std::string>() == edited);
        REQUIRE(column[0].get<std::string>() == edited);
        frame.insert_column(0, "extra", "default");
        REQUIRE_THROWS_AS(column.name(), std::runtime_error);
        REQUIRE(frame.at(0)["value"].get<std::string>() == edited);
        REQUIRE(frame.columns() == std::vector<std::string>{"extra", "id", "value"});
        REQUIRE(frame.column_view("extra").erase());
        auto selected = frame.selected_rows({1});
        REQUIRE(selected.columns() == std::vector<std::string>{"id", "value"});
        REQUIRE(selected.at(0)["value"].get<std::string>() == edited);
        frame.append_column("materialize");
        REQUIRE(frame.column_view("materialize").erase());
        frame = frame.selected_rows({1});
    }
}

#if CSV_ENABLE_THREADS
TEST_CASE("DataFrame: concurrent first edits retain clean readers and captured cells",
    "[data_frame][backend][threading]") {
    const auto& filename = issue_333_file();
    auto validate = [](CSVReader& reader) {
        auto frame = std::make_shared<DataFrame<>>(reader);
        REQUIRE(frame->size() == 500001);
        auto errors = std::make_shared<ThreadSafeErrorCollector>();
        test_with_timeout([frame, errors]() {
            const auto& source = *frame;
            const auto row = source.at(0);
            auto pending_name = frame->at(0)["name"];
            auto pending_value = frame->at(0)["value"];
            const auto* names = &source.columns();
            std::atomic<bool> start(false), valid(true);
            auto wait = [&start]() { while (!start.load(std::memory_order_acquire)) { std::this_thread::yield(); } };
            std::thread name_writer([&]() { wait(); pending_name = "edited name"; });
            std::thread value_writer([&]() { wait(); pending_value = "99"; });
            std::thread observer([&]() {
                wait();
                for (size_t i = 0; i < 2000; ++i) {
                    const auto name = row["name"].get<std::string>();
                    const auto value = row["value"].get<std::string>();
                    if ((name != "person_1" && name != "edited name") ||
                        (value != "7" && value != "99") || &source.columns() != names) {
                        valid.store(false, std::memory_order_relaxed);
                    }
                }
            });
            start.store(true, std::memory_order_release);
            name_writer.join();
            value_writer.join();
            observer.join();
            if (!valid.load(std::memory_order_relaxed)) { errors->add_error("first-edit reader observed corrupt state"); }
            if (row["name"].get<std::string>() != "edited name") { errors->add_error("captured name cell lost its write"); }
            if (row["value"].get<int>() != 99) { errors->add_error("captured value cell lost its write"); }
        });
        errors->check_and_fail_if_errors();
    };
    SECTION("mmap") {
        CSVReader reader(filename, CSVFormat());
        validate(reader);
    }
    SECTION("stream") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, CSVFormat());
        validate(reader);
    }
}
#endif

TEST_CASE("DataFrame: direct column views preserve parsed and edited field bytes",
    "[data_frame][column_view]") {
    static csv_test::GeneratedFile file("csv_parser_direct_column_view.csv");
    const auto& filename = file.path([](std::ofstream& out) {
        out << "id,name,value,quoted,empty\n";
        for (size_t i = 0; i < 500001; ++i) {
            out << i << ", name_" << i << " ," << i * 7
                << ",\" quoted_" << i << "_\"\"x\"\" \",\n";
        }
    });
    CSVFormat format;
    format.trim({' '}).eager_field_classification(true);
    auto validate = [](CSVReader& reader) {
        DataFrame<> frame(reader);
        REQUIRE(frame.size() == 500001);
        const auto& source = frame;
        // Direct extraction must retain CSVRow decoding/trimming even when the
        // parser has cached scalar metadata that this read path does not need.
        for (const size_t row : {size_t(0), size_t(250000), size_t(500000)}) {
            for (size_t column = 0; column < 5; ++column) {
                CHECK(source.column_view(column).get_sv(row) ==
                    source.at(row).get_underlying_row()[column].get_sv());
            }
        }
        CHECK(source.column_view("quoted").get_sv(500000) == "quoted_500000_\"x\"");
        auto names = source.column_view("name");
        frame.at(0)[1] = "edited,\n\"name\"";
        CHECK(names.get_sv(0) == "edited,\n\"name\"");
        CHECK(names.get_sv(500000) == "name_500000");
        CHECK_THROWS_AS(names.get_sv(frame.size()), std::out_of_range);
        REQUIRE(frame.column_view("value").erase());
        CHECK_THROWS_AS(names.get_sv(0), std::runtime_error);
        CHECK(source.column_view("quoted").get_sv(500000) == "quoted_500000_\"x\"");
        CHECK(source.column_view("empty").get_sv(500000).empty());
        // Materialized fields contain literal bytes, with no parser trimming.
        frame.insert_column(1, "new", "  materialized\"value  ");
        CHECK(source.column_view("new").get_sv(500000) == "  materialized\"value  ");
        CHECK(source.column_view("name").get_sv(0) == "edited,\n\"name\"");
        CHECK(source.column_view("quoted").get_sv(500000) == "quoted_500000_\"x\"");
        CHECK(source.column_view("empty").get_sv(500000).empty());
    };
    SECTION("mmap") {
        CSVReader reader(filename, format);
        validate(reader);
    }
    SECTION("stream") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input, format);
        validate(reader);
    }
}
