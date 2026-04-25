#include <sstream>

#include <catch2/catch_all.hpp>
#include "csv.hpp"

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
}
