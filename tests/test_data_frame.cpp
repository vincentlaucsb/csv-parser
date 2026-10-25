#include "csv.hpp"

#if CSV_EXCEPTIONS_ENABLED
#include <sstream>

#include <catch2/catch_all.hpp>
#include "csv.hpp"
#include "shared/file_guard.hpp"
#include "shared/generated_file.hpp"

using namespace csv;

namespace {
    std::istringstream make_people_stream() {
        return std::istringstream(
            "id,name,value\n"
            "1,Alice,10\n"
            "2,Bob,20\n"
            "1,Carol,30\n"
        );
    }
}

TEST_CASE("DataFrame: positional access", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader);

    REQUIRE(frame.size() == 3);
    REQUIRE(frame.columns().size() == 3);
    REQUIRE(frame[0]["id"].get<std::string>() == "1");
    REQUIRE(frame.at(1)["name"].get<std::string>() == "Bob");

    REQUIRE_THROWS_AS(frame["1"], std::runtime_error);
}

TEST_CASE("DataFrame: basic helpers", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader);

    REQUIRE(frame.n_rows() == 3);
    REQUIRE(frame.n_cols() == 3);
    REQUIRE(frame.has_column("name"));
    REQUIRE_FALSE(frame.has_column("missing"));
    REQUIRE(frame.index_of("value") == 2);
    REQUIRE(frame.index_of("missing") == CSV_NOT_FOUND);

    REQUIRE(frame.at(0)["name"].get<std::string>() == "Alice");
    REQUIRE_THROWS_AS(frame.at(99), std::out_of_range);

    REQUIRE(frame.at(2)["name"].get<std::string>() == "Carol");

    const auto& cframe = frame;
    REQUIRE_THROWS_AS(cframe.column_view(cframe.n_cols()), std::out_of_range);
}

TEST_CASE("DataFrame: default cell is empty", "[data_frame]") {
    DataFrameCell cell;

    REQUIRE(cell.get_sv().empty());
    REQUIRE(cell.is_null());
}

TEST_CASE("DataFrame: preserves CSVReader column name policy", "[data_frame]") {
    auto input = make_people_stream();
    CSVFormat format;
    format.column_names_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    CSVReader reader(input, format);
    DataFrame<> frame(reader);

    REQUIRE(frame.has_column("NAME"));
    REQUIRE(frame.index_of("VALUE") == 2);
    REQUIRE(frame.at(0)["NAME"].get<std::string>() == "Alice");
}

TEST_CASE("DataFrame: construct from row batch", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    std::vector<CSVRow> rows(reader.begin(), reader.end());

    DataFrame<> frame(std::move(rows));

    REQUIRE(frame.size() == 3);
    REQUIRE(frame.columns().size() == 3);
    REQUIRE(frame.at(0)["id"].get<std::string>() == "1");
    REQUIRE(frame.at(1)["name"].get<std::string>() == "Bob");
    REQUIRE(frame.at(2)["value"].get<std::string>() == "30");
}

TEST_CASE("DataFrame: row batch preserves column name policy", "[data_frame]") {
    auto input = make_people_stream();
    CSVFormat format;
    format.column_names_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    CSVReader reader(input, format);
    std::vector<CSVRow> rows(reader.begin(), reader.end());

    DataFrame<> frame(std::move(rows));

    REQUIRE(frame.has_column("NAME"));
    REQUIRE(frame.index_of("VALUE") == 2);
    REQUIRE(frame.at(1)["NAME"].get<std::string>() == "Bob");
}

TEST_CASE("DataFrame: row-wise iteration", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader);

    std::vector<std::string> names;
    for (auto& row : frame) {
        names.push_back(row["name"].get<std::string>());
    }

    REQUIRE(names.size() == 3);
    REQUIRE(names[0] == "Alice");
    REQUIRE(names[1] == "Bob");
    REQUIRE(names[2] == "Carol");

    auto it = frame.begin();
    ++it;
    REQUIRE((*it)["name"].get<std::string>() == "Bob");

    const auto& cframe = frame;
    size_t count = 0;
    for (auto cit = cframe.cbegin(); cit != cframe.cend(); ++cit) {
        REQUIRE((*cit)["id"].get<std::string>().empty() == false);
        count++;
    }
    REQUIRE(count == 3);
}

TEST_CASE("DataFrame: column iteration respects visible values", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

    frame["1"]["name"] = "Alicia";

    auto name_col = frame.column_view("name");
    std::vector<std::string> values;
    for (const auto& cell : name_col) {
        values.push_back(cell.get<std::string>());
    }

    REQUIRE(name_col.name() == "name");
    REQUIRE(name_col.index() == 1);
    REQUIRE(name_col.size() == 2);
    REQUIRE(values == std::vector<std::string>{"Alicia", "Bob"});
    REQUIRE(name_col.begin()->get<std::string>() == "Alicia");
    REQUIRE(name_col.get_sv(0) == "Alicia");
    REQUIRE(name_col.get_sv(1) == "Bob");

    auto value_col = frame.column_view("value");
    REQUIRE(value_col.get_sv(0) == "10");
    REQUIRE_THROWS_AS(frame.column_view(frame.n_cols()), std::out_of_range);
}

TEST_CASE("DataFrame: keyed access with overwrite and lazy index", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader, "id");

    REQUIRE(frame.size() == 2);
    REQUIRE(frame.contains("1"));
    REQUIRE(frame.contains("2"));

    REQUIRE(frame["1"]["name"].get<std::string>() == "Carol");
    REQUIRE(frame["1"]["value"].get<std::string>() == "30");
    REQUIRE(frame.at(0).key() == "1");
}

TEST_CASE("DataFrame: keyed helpers", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader, "id");

    REQUIRE(frame["1"]["name"].get<std::string>() == "Carol");
    REQUIRE_THROWS_AS(frame["missing"], std::out_of_range);

    REQUIRE(frame["2"]["name"].get<std::string>() == "Bob");
    REQUIRE(frame["2"].key() == "2");

    frame.at(0)["name"] = "Carly";
    REQUIRE(frame["1"]["name"].get<std::string>() == "Carly");

    // Verify edits are visible through all access methods
    REQUIRE(frame.at(0)["name"].get<std::string>() == "Carly");
    REQUIRE(frame["1"]["name"].get<std::string>() == "Carly");
    REQUIRE(frame.at(0)["name"].get<std::string>() == "Carly");
    REQUIRE(frame["1"].to_json() == "{\"id\":1,\"name\":\"Carly\",\"value\":30}");
    REQUIRE(frame["1"].to_json_array() == "[1,\"Carly\",30]");
    
    // Verify edits are visible through iteration
    bool found_carly = false;
    for (auto& df_row : frame) {
        std::string name = df_row["name"].get<std::string>();
        if (name == "Carly") {
            found_carly = true;
        }
    }
    REQUIRE(found_carly);

    // Verify edits are visible through keyed const iteration (covers const_iterator edit overlay path)
    const auto& cframe = frame;
    bool found_carly_const = false;
    bool found_bob_const = false;
    for (auto cit = cframe.cbegin(); cit != cframe.cend(); ++cit) {
        std::string name = (*cit)["name"].get<std::string>();
        if (name == "Carly") found_carly_const = true;
        if (name == "Bob") found_bob_const = true;
    }
    REQUIRE(found_carly_const);
    REQUIRE(found_bob_const);

    // Verify DataFrameRow stores key and can be converted to vector
    auto row_0 = frame.at(0);
    REQUIRE(row_0.key() == "1");
    std::vector<std::string> vec = row_0;
    REQUIRE(vec.size() == 3);
    REQUIRE(vec[1] == "Carly");  // Edited value

    REQUIRE(frame.at(0).erase());
    REQUIRE_FALSE(frame.contains("1"));
    REQUIRE(frame.size() == 1);
}

TEST_CASE("DataFrame: detached row JSON respects sparse overlay edits", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    std::vector<CSVRow> rows(reader.begin(), reader.end());

    RowOverlay overlay;
    overlay.set(1, "DetachedCarol");
    overlay.set(2, "42");

    DataFrameRow<std::string> detached_row(&rows[2], static_cast<const DataFrame<std::string>*>(nullptr), 2, &overlay, nullptr);

    REQUIRE(detached_row["name"].get<std::string>() == "DetachedCarol");
    REQUIRE(detached_row["value"].get<std::string>() == "42");
    REQUIRE(detached_row["id"].get<std::string>() == "1");
    REQUIRE(detached_row.to_json() == "{\"id\":1,\"name\":\"DetachedCarol\",\"value\":42}");
    REQUIRE(detached_row.to_json_array() == "[1,\"DetachedCarol\",42]");
    REQUIRE(detached_row.to_json(std::vector<std::string>{"name", "value"}) == "{\"name\":\"DetachedCarol\",\"value\":42}");
    REQUIRE(detached_row.to_json_array(std::vector<std::string>{"name", "value"}) == "[\"DetachedCarol\",42]");
    REQUIRE_THROWS_AS(detached_row["missing"], std::out_of_range);
}

TEST_CASE("DataFrame: arbitrary key function", "[data_frame]") {
    SECTION("Scalar Value") {
        auto input = make_people_stream();
        CSVReader reader(input);

        DataFrame<int> frame(
            reader,
            [](const CSVRow& row) {
                return row["value"].get<int>() / 10;
            },
            DataFrame<int>::DuplicateKeyPolicy::KEEP_FIRST
        );

        REQUIRE(frame.size() == 3);
        REQUIRE(frame.contains(1));
        REQUIRE(frame.contains(2));
        REQUIRE(frame.contains(3));

        REQUIRE(frame[1]["name"].get<std::string>() == "Alice");
        REQUIRE(frame[2]["name"].get<std::string>() == "Bob");
        REQUIRE(frame[3]["name"].get<std::string>() == "Carol");
    }

    #ifndef __EMSCRIPTEN__
    SECTION("Tuple-ish Value") {
        CSVReader reader("./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2014_c20170718.csv");

        DataFrame<std::string> frame(
            reader,
            [](const CSVRow& row) {
                std::string yearMonth = row["YEARMONTH"].get<std::string>();
                std::string loc = row["LOCATION"].get<std::string>();
                return yearMonth + "-" + loc;
            },
            DataFrame<std::string>::DuplicateKeyPolicy::KEEP_FIRST
        );

        REQUIRE(frame.contains("201405-BAKERSFIELD"));
    }
    #endif

    SECTION("Duplicate key policy THROW") {
        auto input = make_people_stream();
        CSVReader reader(input);

        REQUIRE_THROWS_AS(
            DataFrame<int>(
                reader,
                [](const CSVRow&) { return 1; },
                DataFrame<int>::DuplicateKeyPolicy::THROW
            ),
            std::runtime_error
        );
    }

    SECTION("Duplicate key policy OVERWRITE") {
        auto input = make_people_stream();
        CSVReader reader(input);

        DataFrame<int> frame(
            reader,
            [](const CSVRow&) { return 1; },
            DataFrame<int>::DuplicateKeyPolicy::OVERWRITE
        );

        REQUIRE(frame.size() == 1);
        REQUIRE(frame[1]["name"].get<std::string>() == "Carol");
    }
}

TEST_CASE("DataFrame: group_by", "[data_frame]") {
    SECTION("Group by arbitrary function") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

        auto grouped = frame.group_by([](DataFrameRow<std::string> row) {
            int value = row["value"].get<int>();
            return value >= 20 ? std::string("high") : std::string("low");
        });

        REQUIRE(grouped.size() == 2);
        REQUIRE(grouped["low"].size() == 1);
        REQUIRE(grouped["high"].size() == 1);

        REQUIRE(frame.at(grouped["low"][0])["name"].get<std::string>() == "Alice");
        REQUIRE(frame.at(grouped["high"][0])["name"].get<std::string>() == "Bob");
    }

    SECTION("Group by column honors edits") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrame<> frame(reader, "id");

        frame["2"]["name"] = "Bobby";

        auto grouped = frame.group_by("name");

        REQUIRE(grouped.size() == 2);
        REQUIRE(grouped["Carol"].size() == 1);
        REQUIRE(grouped["Bobby"].size() == 1);
    }

    SECTION("Missing column throws") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrame<> frame(reader, "id");

        REQUIRE_THROWS_AS(frame.group_by("missing"), std::out_of_range);
    }
}

#ifndef __EMSCRIPTEN__
TEST_CASE("DataFrame: group_by on NOAA real data", "[data_frame]") {
    CSVReader reader("./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2014_c20170718.csv");
    DataFrame<> frame(reader);

    SECTION("Column grouping matches function grouping") {
        auto by_column = frame.group_by("YEARMONTH");
        auto by_function = frame.group_by([](DataFrameRow<std::string> row) {
            return row["YEARMONTH"].get<std::string>();
        });

        REQUIRE(by_column.size() == by_function.size());

        for (const auto& entry : by_column) {
            auto it = by_function.find(entry.first);
            REQUIRE(it != by_function.end());
            REQUIRE(it->second.size() == entry.second.size());
        }
    }

    SECTION("Function grouping forms a complete partition") {
        auto grouped = frame.group_by([](DataFrameRow<std::string> row) {
            int yearmonth = row["YEARMONTH"].get<int>();
            int month = yearmonth % 100;
            if (month <= 3) return std::string("Q1");
            if (month <= 6) return std::string("Q2");
            if (month <= 9) return std::string("Q3");
            return std::string("Q4");
        });

        REQUIRE(grouped.size() == 4);

        std::vector<bool> seen(frame.size(), false);
        size_t assigned = 0;

        for (const auto& group_entry : grouped) {
            for (size_t idx : group_entry.second) {
                REQUIRE(idx < frame.size());
                REQUIRE_FALSE(seen[idx]);
                seen[idx] = true;
                assigned++;
            }
        }

        REQUIRE(assigned == frame.size());
    }

    SECTION("DataFrame: group_by YEARMONTH + LOCATION spot check") {
        auto grouped = frame.group_by([](DataFrameRow<std::string> row) {
            std::string yearmonth = row["YEARMONTH"].get<std::string>();
            std::string location = row["LOCATION"].get<std::string>();
            return yearmonth + "|" + location;
        });

        auto it = grouped.find("201405|BAKERSFIELD");
        REQUIRE(it != grouped.end());

        bool found = false;
        for (size_t idx : it->second) {
            const auto& row = frame.at(idx);
            if (row["LATITUDE"].get<std::string>() == "30.909" &&
                row["LONGITUDE"].get<std::string>() == "-102.28") {
                found = true;
                break;
            }
        }

        REQUIRE(found);
    }
}
#endif

#ifndef __EMSCRIPTEN__
TEST_CASE("DataFrame: filename + options + format", "[data_frame]") {
    DataFrameOptions options;
    options.set_key_column("A")
        .set_duplicate_key_policy(DataFrameOptions::DuplicateKeyPolicy::OVERWRITE);

    CSVFormat format;
    format.delimiter(',').header_row(0);

    DataFrame<> frame(
        "./tests/data/fake_data/ints_squared.csv",
        options,
        format
    );

    REQUIRE(frame.size() == 100);
    REQUIRE(frame.contains("1"));
    REQUIRE(frame.contains("100"));
    REQUIRE(frame["50"]["B"].get<std::string>() == "2500");
}
#endif

TEST_CASE("DataFrame: options validation", "[data_frame]") {
    SECTION("Empty key column") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrameOptions options;

        REQUIRE_THROWS_AS(DataFrame<>(reader, options), std::runtime_error);
    }

    SECTION("Missing key column") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrameOptions options;
        options.set_key_column("missing");

        REQUIRE_THROWS_AS(DataFrame<>(reader, options), std::runtime_error);
    }

    SECTION("Throw on missing key value") {
        std::istringstream input("id,name\n,Blank\n1,Alice\n");
        CSVReader reader(input);
        DataFrameOptions options;
        options.set_key_column("id").set_throw_on_missing_key(true);

        REQUIRE_THROWS_AS(DataFrame<int>(reader, options), std::runtime_error);
    }

    SECTION("Allow missing key value") {
        std::istringstream input("id,name\n,Blank\n1,Alice\n");
        CSVReader reader(input);
        DataFrameOptions options;
        options.set_key_column("id").set_throw_on_missing_key(false);

        DataFrame<int> frame(reader, options);
        REQUIRE(frame.size() == 2);
        REQUIRE(frame.contains(0));
        REQUIRE(frame.contains(1));
    }

    SECTION("Allow malformed key value") {
        std::istringstream input("id,name\nbad,Blank\n1,Alice\n");
        CSVReader reader(input);
        DataFrameOptions options;
        options.set_key_column("id").set_throw_on_missing_key(false);

        DataFrame<int> frame(reader, options);
        REQUIRE(frame.size() == 2);
        REQUIRE(frame.contains(0));
        REQUIRE(frame[0]["name"].get<std::string>() == "Blank");
        REQUIRE(frame.contains(1));
    }
}

TEST_CASE("DataFrame: duplicate key policies", "[data_frame]") {
    SECTION("KEEP_FIRST") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

        REQUIRE(frame.size() == 2);
        REQUIRE(frame["1"]["name"].get<std::string>() == "Alice");
        REQUIRE(frame["1"]["value"].get<std::string>() == "10");
    }

    SECTION("THROW") {
        auto input = make_people_stream();
        CSVReader reader(input);

        REQUIRE_THROWS_AS(
            DataFrame<>(reader, "id", DataFrameOptions::DuplicateKeyPolicy::THROW),
            std::runtime_error
        );
    }
}

TEST_CASE("DataFrame: edit overlay and column extraction", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader, "id");

    REQUIRE(frame["2"]["name"].get<std::string>() == "Bob");

    frame["2"]["name"] = "Bobby";
    frame["1"]["value"] = "31";

    REQUIRE(frame["2"]["name"].get<std::string>() == "Bobby");
    REQUIRE(frame["1"]["value"].get<std::string>() == "31");

    auto names = frame.column("name");
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "Carol");
    REQUIRE(names[1] == "Bobby");

    REQUIRE(frame["2"].erase());
    REQUIRE_FALSE(frame.contains("2"));
    REQUIRE(frame.size() == 1);

    REQUIRE_THROWS_AS(frame.column("missing"), std::out_of_range);
}

TEST_CASE("DataFrame: cell assignment error handling", "[data_frame]") {
    SECTION("supports unkeyed DataFrame") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrame<> frame(reader);  // no key column

        frame[0]["name"] = "X";
        REQUIRE(frame.at(0)["name"].get<std::string>() == "X");
    }

    SECTION("throws on out-of-range row index") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrame<> frame(reader, "id");

        REQUIRE_THROWS_AS(frame.at(99)["name"] = "X", std::out_of_range);
    }

    SECTION("throws on unknown column") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrame<> frame(reader, "id");

        REQUIRE_THROWS_AS(frame.at(0)["nonexistent"] = "X", std::out_of_range);
    }
}

TEST_CASE("DataFrame: cell assignment overwrites previous edit", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader, "id");

    // First edit
    frame.at(0)["name"] = "First";
    REQUIRE(frame["1"]["name"].get<std::string>() == "First");

    // Second edit to same cell overwrites the first
    frame.at(0)["name"] = "Second";
    REQUIRE(frame["1"]["name"].get<std::string>() == "Second");

    // Other cells are unaffected
    REQUIRE(frame["1"]["value"].get<std::string>() == "30");
    REQUIRE(frame["2"]["name"].get<std::string>() == "Bob");
}

TEST_CASE("DataFrame: cell assignment tracks independent edits across rows and columns", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader, "id");

    frame.at(0)["name"] = "NewName1";
    frame.at(0)["value"] = "99";
    frame.at(1)["name"] = "NewName2";

    REQUIRE(frame["1"]["name"].get<std::string>() == "NewName1");
    REQUIRE(frame["1"]["value"].get<std::string>() == "99");
    REQUIRE(frame["2"]["name"].get<std::string>() == "NewName2");
    REQUIRE(frame["2"]["value"].get<std::string>() == "20");  // unedited

    // column() extraction reflects all edits
    auto names = frame.column("name");
    REQUIRE(names[0] == "NewName1");
    REQUIRE(names[1] == "NewName2");
}


TEST_CASE("DataFrame: row insert", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
        "4,Carol,30\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

    frame.insert_row(2, { "3", "Charlie", "25" });

    REQUIRE(frame.size() == 4);
    REQUIRE(frame.at(2)["name"].get<std::string>() == "Charlie");
    REQUIRE(frame["3"]["value"].get<std::string>() == "25");
    REQUIRE(frame.at(3)["id"].get<std::string>() == "4");
    REQUIRE(frame.at(3).key() == "4");
}

TEST_CASE("DataFrame: row insert rejects non-empty rows without schema", "[data_frame]") {
    DataFrame<> frame;

    REQUIRE_THROWS_AS(frame.insert_row(0, { "orphaned" }), std::invalid_argument);

    frame.insert_row(0, {});
    REQUIRE(frame.size() == 1);
    REQUIRE(frame.n_cols() == 0);
    REQUIRE(frame.at(0).empty());
}

TEST_CASE("DataFrame: inserted row stores logical field values", "[data_frame]") {
    std::istringstream input(
        "id,name,note\n"
        "1,Alice,plain\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader);

    frame.insert_row(1, { "2", "Comma, Quote\"", "line 1\nline 2" });

    REQUIRE(frame.size() == 2);
    REQUIRE(frame.at(1)["id"].get<std::string>() == "2");
    REQUIRE(frame.at(1)["name"].get<std::string>() == "Comma, Quote\"");
    REQUIRE(frame.at(1)["note"].get<std::string>() == "line 1\nline 2");
}

TEST_CASE("DataFrame: row insert validates shape and duplicate keys", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

    REQUIRE_THROWS_AS(frame.insert_row(3, { "3", "Carol", "30" }), std::out_of_range);
    REQUIRE_THROWS_AS(frame.insert_row(1, { "3", "Carol" }), std::invalid_argument);
    REQUIRE_THROWS_AS(frame.insert_row(1, { "2", "Bobby", "21" }), std::runtime_error);
    REQUIRE(frame.size() == 2);
    REQUIRE(frame["2"]["name"].get<std::string>() == "Bob");
}

TEST_CASE("DataFrame: row insert rejects custom-keyed frames", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(
        reader,
        [](const CSVRow& row) {
            return row["name"].get<std::string>();
        },
        DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST
    );

    REQUIRE_THROWS_AS(frame.insert_row(1, { "3", "Carol", "30" }), std::runtime_error);
}

TEST_CASE("DataFrame: column insert materializes visible rows", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader);

    frame.at(0)["name"] = "Alicia";
    frame.insert_column(1, "note", "x,y");

    REQUIRE(frame.n_cols() == 4);
    REQUIRE(frame.columns() == std::vector<std::string>{"id", "note", "name", "value"});
    REQUIRE(frame.at(0)["id"].get<std::string>() == "1");
    REQUIRE(frame.at(0)["note"].get<std::string>() == "x,y");
    REQUIRE(frame.at(0)["name"].get<std::string>() == "Alicia");
    REQUIRE(frame.at(1)["note"].get<std::string>() == "x,y");
    REQUIRE(frame.column("note") == std::vector<std::string>{"x,y", "x,y"});
    REQUIRE(frame.at(0).to_json() == "{\"id\":1,\"note\":\"x,y\",\"name\":\"Alicia\",\"value\":10}");

    frame.at(0)["note"] = "edited";
    REQUIRE(frame.at(0)["note"].get<std::string>() == "edited");

    std::stringstream output;
    auto writer = make_csv_writer(output);
    writer << frame.columns();
    for (auto& row : frame) {
        writer << std::vector<std::string>(row);
    }

    REQUIRE(output.str() ==
        "id,note,name,value\n"
        "1,edited,Alicia,10\n"
        "2,\"x,y\",Bob,20\n"
    );
}

TEST_CASE("DataFrame: column insert preserves keyed access", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

    frame.insert_column(0, "prefix", "row");

    REQUIRE(frame.n_cols() == 4);
    REQUIRE(frame.contains("1"));
    REQUIRE(frame.contains("2"));
    REQUIRE(frame["1"]["prefix"].get<std::string>() == "row");
    REQUIRE(frame["1"]["name"].get<std::string>() == "Alice");
    REQUIRE(frame["2"].key() == "2");
}

TEST_CASE("DataFrame: column insert preserves custom-keyed access", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(
        reader,
        [](const CSVRow& row) {
            return row["name"].get<std::string>();
        },
        DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST
    );

    frame.insert_column(2, "note", "ok");

    REQUIRE(frame.contains("Alice"));
    REQUIRE(frame.contains("Bob"));
    REQUIRE(frame["Alice"]["note"].get<std::string>() == "ok");
    REQUIRE(frame["Bob"]["value"].get<std::string>() == "20");
}

TEST_CASE("DataFrame: column insert validates position and names", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVFormat format;
    format.column_names_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    CSVReader reader(input, format);
    DataFrame<> frame(reader);

    REQUIRE_THROWS_AS(frame.insert_column(4, "extra"), std::out_of_range);
    REQUIRE_THROWS_AS(frame.insert_column(1, ""), std::invalid_argument);
    REQUIRE_THROWS_AS(frame.insert_column(1, "NAME"), std::invalid_argument);

    frame.append_column("extra", "ok");
    REQUIRE(frame.n_cols() == 4);
    REQUIRE(frame.has_column("EXTRA"));
    REQUIRE(frame.at(0)["EXTRA"].get<std::string>() == "ok");
}

TEST_CASE("DataFrame: column insert supports empty frames", "[data_frame]") {
    DataFrame<> frame;

    frame.insert_column(0, "A", "unused");
    frame.append_column("B");

    REQUIRE(frame.n_rows() == 0);
    REQUIRE(frame.n_cols() == 2);
    REQUIRE(frame.columns() == std::vector<std::string>{"A", "B"});
}

TEST_CASE("DataFrame: column erase hides the column without reparsing rows", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader);

    frame.at(0)["name"] = "Alicia";
    auto name_column = frame.column_view("name");
    REQUIRE(name_column.erase());

    REQUIRE(frame.n_cols() == 2);
    REQUIRE(frame.columns() == std::vector<std::string>{"id", "value"});
    REQUIRE_FALSE(frame.has_column("name"));
    REQUIRE(frame.at(0).size() == 2);
    REQUIRE(frame.at(0)["id"].get<std::string>() == "1");
    REQUIRE(frame.at(0)["value"].get<std::string>() == "10");
    REQUIRE(frame.column("value") == std::vector<std::string>{"10", "20"});
    REQUIRE(frame.at(0).to_json() == "{\"id\":1,\"value\":10}");

    std::stringstream output;
    auto writer = make_csv_writer(output);
    writer << frame.columns();
    for (auto& row : frame) {
        writer << std::vector<std::string>(row);
    }

    REQUIRE(output.str() ==
        "id,value\n"
        "1,10\n"
        "2,20\n"
    );
}

TEST_CASE("DataFrame: row insert after column erase expands to physical storage", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

    REQUIRE(frame.column_view("name").erase());
    frame.insert_row(1, { "3", "30" });

    REQUIRE(frame.size() == 3);
    REQUIRE(frame.columns() == std::vector<std::string>{"id", "value"});
    REQUIRE(frame["3"]["value"].get<std::string>() == "30");
    REQUIRE(frame.at(2)["value"].get<std::string>() == "20");
}

TEST_CASE("DataFrame: column erase rejects key and const columns", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

    REQUIRE_THROWS_AS(frame.column_view("id").erase(), std::runtime_error);

    const auto& cframe = frame;
    auto const_column = cframe.column_view("name");
    REQUIRE_THROWS_AS(const_column.erase(), std::runtime_error);

    REQUIRE(frame.column_view("name").erase());
    REQUIRE(frame.contains("1"));
    REQUIRE(frame["1"]["value"].get<std::string>() == "10");
}

TEST_CASE("DataFrame: erased column proxy is invalidated", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader);

    auto name_column = frame.column_view("name");
    REQUIRE(name_column.erase());
    REQUIRE_THROWS_AS(name_column.erase(), std::runtime_error);

    REQUIRE(name_column.size() == 0);
    REQUIRE_THROWS_AS(name_column.name(), std::runtime_error);
    REQUIRE_THROWS_AS(name_column[0], std::runtime_error);
    REQUIRE_THROWS_AS(name_column.get_sv(0), std::runtime_error);

    size_t iterated = 0;
    for (const auto& field : name_column) {
        (void)field;
        ++iterated;
    }
    REQUIRE(iterated == 0);
}

TEST_CASE("DataFrame: row erase shifts sparse edit indices", "[data_frame]") {
    std::istringstream input(
        "id,name,value\n"
        "1,Alice,10\n"
        "2,Bob,20\n"
        "3,Carol,30\n"
    );
    CSVReader reader(input);
    DataFrame<> frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

    frame.at(1)["name"] = "Bobby";
    frame.at(2)["value"] = "31";

    REQUIRE(frame.at(0).erase());

    REQUIRE(frame.size() == 2);
    REQUIRE_FALSE(frame.contains("1"));

    // The later-row edits should stay attached to their rows after the index shift.
    REQUIRE(frame.at(0).key() == "2");
    REQUIRE(frame.at(0)["name"].get<std::string>() == "Bobby");
    REQUIRE(frame.at(0)["value"].get<std::string>() == "20");

    REQUIRE(frame.at(1).key() == "3");
    REQUIRE(frame.at(1)["name"].get<std::string>() == "Carol");
    REQUIRE(frame.at(1)["value"].get<std::string>() == "31");

    auto names = frame.column("name");
    REQUIRE(names == std::vector<std::string>{"Bobby", "Carol"});
}

TEST_CASE("DataFrame: const row erase is rejected", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader, "id");
    const auto& cframe = frame;

    auto row = cframe["1"];
    REQUIRE_THROWS_AS(row.erase(), std::runtime_error);
    REQUIRE_THROWS_AS(row["name"] = "Blocked", std::runtime_error);
}

TEST_CASE("DataFrame: selected row masks keep boundary behavior", "[data_frame][selected_rows]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame<> frame(reader);
    // The unedited path must continue to work without any allocated edits.
    auto all = frame.selected_rows({1, 1, 1});
    REQUIRE(all.column("name") == std::vector<std::string>{"Alice", "Bob", "Carol"});
    frame.at(1)["value"] = "95";

    REQUIRE_THROWS_AS(frame.selected_rows({1, 0}), std::invalid_argument);
    REQUIRE_THROWS_AS(frame.selected_rows({1, 0, 1, 0}), std::invalid_argument);
    REQUIRE_THROWS_AS(frame.selected_rows({}), std::invalid_argument);
    REQUIRE(frame.at(1)["value"].get<std::string>() == "95");
    auto none = frame.selected_rows({0, 0, 0});
    REQUIRE(none.empty());
    REQUIRE(none.columns() == frame.columns());
    REQUIRE(none.selected_rows({}).columns() == frame.columns());
    auto edited_all = frame.selected_rows({1, 1, 1});
    REQUIRE(edited_all.column("value") == std::vector<std::string>{"10", "95", "30"});

    DataFrame<> empty;
    REQUIRE(empty.selected_rows({}).empty());
    REQUIRE_THROWS_AS(empty.selected_rows({1}), std::invalid_argument);
}


TEST_CASE("DataFrame: load edit and save a document", "[data_frame][document_io]") {
    FileGuard input_file("./tests/data/fake_data/dataframe_document_io.csv");
    {
        std::ofstream out(input_file.filename, std::ios::binary);
        out << "id;name;value\n1;Alice;10\n2;Bob;20\n";
    }
    auto validate = [](DataFrame<>& frame) {
        REQUIRE(frame.columns() == std::vector<std::string>{"id", "name", "value"});
        REQUIRE(frame.n_rows() == 2);
        REQUIRE_THROWS_AS(frame["1"], std::runtime_error);
        frame.column_view("name")[1] = "new, \"quoted\"\nname";
        const auto read_only_column = frame.column_view("name");
        REQUIRE_THROWS_AS(read_only_column[1] = "blocked", std::runtime_error);
        const auto& read_only_frame = frame;
        REQUIRE_THROWS_AS(read_only_frame.column_view("name")[1] = "blocked", std::runtime_error);
        std::stringstream output;
        auto writer = make_csv_writer(output);
        const auto& document = frame;
        REQUIRE(&(writer << document) == &writer);
        REQUIRE(output.str() == "id,name,value\n1,Alice,10\n2,\"new, \"\"quoted\"\"\nname\",20\n");
        CSVReader reader(output);
        DataFrame<> round_trip(reader);
        REQUIRE(round_trip.columns() == frame.columns());
        REQUIRE(round_trip.n_rows() == frame.n_rows());
        for (size_t i = 0; i < frame.n_rows(); ++i) {
            REQUIRE(std::vector<std::string>(round_trip.at(i)) == std::vector<std::string>(frame.at(i)));
        }
    };
    SECTION("Memory-mapped filename with format guessing") {
        DataFrame<> frame(input_file.filename);
        validate(frame);
    }
    SECTION("Memory-mapped filename with explicit format") {
        DataFrame<> frame(input_file.filename, CSVFormat().delimiter(';').header_row(0));
        validate(frame);
    }
    SECTION("Existing stream reader path") {
        std::ifstream input(input_file.filename, std::ios::binary);
        CSVReader reader(input, CSVFormat().delimiter(';').header_row(0));
        DataFrame<> frame(reader);
        validate(frame);
    }
}

TEST_CASE("DataFrame: filename constructor propagates reader errors", "[data_frame][document_io]") {
    REQUIRE_THROWS_AS(DataFrame<>("./tests/data/fake_data/no_such_dataframe_file.csv"), std::runtime_error);
}

TEST_CASE("DataFrame: document round trip across parser chunks", "[data_frame][document_io]") {
    // Distinct columns and more than 10 MB catch chunk lifetime or field mix-ups.
    static csv_test::GeneratedFile fixture("dataframe_document_large.csv");
    const auto& filename = fixture.path([](std::ostream& out) {
        out << "id,label,value\n";
        for (size_t i = 0; i < 500000; ++i) {
            out << i << ",label-for-row-" << i << ',' << i * 7 + 3 << '\n';
        }
    });
    auto validate = [](const DataFrame<>& frame) {
        REQUIRE(frame.n_rows() == 500000);
        std::stringstream output;
        auto writer = make_csv_writer(output).set_auto_flush(false);
        writer << frame;
        writer.flush();
        CSVReader reader(output);
        size_t i = 0;
        for (const auto& row : reader) {
            REQUIRE(row[0].get<size_t>() == i);
            REQUIRE(row[1].get<std::string>() == "label-for-row-" + std::to_string(i));
            REQUIRE(row[2].get<size_t>() == i * 7 + 3);
            ++i;
        }
        REQUIRE(i == 500000);
    };
    SECTION("Memory-mapped filename") {
        const DataFrame<> frame(filename);
        validate(frame);
    }
    SECTION("Stream reader") {
        std::ifstream input(filename, std::ios::binary);
        CSVReader reader(input);
        const DataFrame<> frame(reader);
        validate(frame);
    }
}
#endif

#include "shared/explicit_error_helpers.hpp"

using namespace csv;

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
