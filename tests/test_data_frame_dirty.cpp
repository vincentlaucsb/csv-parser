#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/generated_file.hpp"

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
