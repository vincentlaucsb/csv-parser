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
    DataFrame frame(reader);

    REQUIRE(frame.size() == 3);
    REQUIRE(frame.columns().size() == 3);
    REQUIRE(frame[0]["id"].get<std::string>() == "1");
    REQUIRE(frame.iloc(1)["name"].get<std::string>() == "Bob");

    REQUIRE_THROWS_AS(frame["1"], std::runtime_error);
}

TEST_CASE("DataFrame: basic helpers", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame frame(reader);

    REQUIRE(frame.n_rows() == 3);
    REQUIRE(frame.n_cols() == 3);
    REQUIRE(frame.has_column("name"));
    REQUIRE_FALSE(frame.has_column("missing"));
    REQUIRE(frame.index_of("value") == 2);
    REQUIRE(frame.index_of("missing") == CSV_NOT_FOUND);

    REQUIRE(frame.at(0)["name"].get<std::string>() == "Alice");
    REQUIRE_THROWS_AS(frame.at(99), std::out_of_range);

    csv::DataFrameRow<std::string> row;
    REQUIRE(frame.try_get(2, row));
    REQUIRE(row["name"].get<std::string>() == "Carol");
    REQUIRE_FALSE(frame.try_get(99, row));
}

TEST_CASE("DataFrame: row-wise iteration", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame frame(reader);

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

TEST_CASE("DataFrame: keyed access with overwrite and lazy index", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame frame(reader, "id");

    REQUIRE(frame.size() == 2);
    REQUIRE(frame.key_name() == "id");
    REQUIRE(frame.contains("1"));
    REQUIRE(frame.contains("2"));

    REQUIRE(frame["1"]["name"].get<std::string>() == "Carol");
    REQUIRE(frame["1"]["value"].get<std::string>() == "30");
    REQUIRE(frame.key_at(0) == "1");
}

TEST_CASE("DataFrame: keyed helpers", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame frame(reader, "id");

    REQUIRE(frame.at("1")["name"].get<std::string>() == "Carol");
    REQUIRE_THROWS_AS(frame.at("missing"), std::out_of_range);

    csv::DataFrameRow<std::string> row;
    REQUIRE(frame.try_get("2", row));
    REQUIRE(row["name"].get<std::string>() == "Bob");
    REQUIRE_FALSE(frame.try_get("missing", row));

    frame.set_at(0, "name", "Carly");
    REQUIRE(frame.get("1", "name") == "Carly");
    
    // Verify edits are visible through all access methods
    REQUIRE(frame.at(0)["name"].get<std::string>() == "Carly");
    REQUIRE(frame["1"]["name"].get<std::string>() == "Carly");
    REQUIRE(frame.iloc(0)["name"].get<std::string>() == "Carly");
    
    // Verify edits are visible through iteration
    bool found_carly = false;
    for (auto& row : frame) {
        std::string name = row["name"].get<std::string>();
        if (name == "Carly") {
            found_carly = true;
        }
    }
    REQUIRE(found_carly);
    
    // Verify DataFrameRow stores key and can be converted to vector
    auto row_0 = frame.at(0);
    REQUIRE(row_0.get_key() == "1");
    std::vector<std::string> vec = row_0;
    REQUIRE(vec.size() == 3);
    REQUIRE(vec[1] == "Carly");  // Edited value

    REQUIRE_FALSE(frame.erase_row_at(99));
    REQUIRE(frame.erase_row_at(0));
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
        DataFrame frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

        auto grouped = frame.group_by([](const CSVRow& row) {
            int value = row["value"].get<int>();
            return value >= 20 ? std::string("high") : std::string("low");
        });

        REQUIRE(grouped.size() == 2);
        REQUIRE(grouped["low"].size() == 1);
        REQUIRE(grouped["high"].size() == 1);

        REQUIRE(frame.iloc(grouped["low"][0])["name"].get<std::string>() == "Alice");
        REQUIRE(frame.iloc(grouped["high"][0])["name"].get<std::string>() == "Bob");
    }

    SECTION("Group by column honors edits") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrame frame(reader, "id");

        frame.set("2", "name", "Bobby");

        auto grouped = frame.group_by("name");

        REQUIRE(grouped.size() == 2);
        REQUIRE(grouped["Carol"].size() == 1);
        REQUIRE(grouped["Bobby"].size() == 1);
    }

    SECTION("Missing column throws") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrame frame(reader, "id");

        REQUIRE_THROWS_AS(frame.group_by("missing"), std::runtime_error);
    }
}

TEST_CASE("DataFrame: group_by on NOAA real data", "[data_frame]") {
    CSVReader reader("./tests/data/real_data/noaa_storm_events/StormEvents_locations-ftp_v1.0_d2014_c20170718.csv");
    DataFrame frame(reader);

    SECTION("Column grouping matches function grouping") {
        auto by_column = frame.group_by("YEARMONTH");
        auto by_function = frame.group_by([](const CSVRow& row) {
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
        auto grouped = frame.group_by([](const CSVRow& row) {
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
        auto grouped = frame.group_by([](const CSVRow& row) {
            std::string yearmonth = row["YEARMONTH"].get<std::string>();
            std::string location = row["LOCATION"].get<std::string>();
            return yearmonth + "|" + location;
        });

        auto it = grouped.find("201405|BAKERSFIELD");
        REQUIRE(it != grouped.end());

        bool found = false;
        for (size_t idx : it->second) {
            const auto& row = frame.iloc(idx);
            if (row["LATITUDE"].get<std::string>() == "30.909" &&
                row["LONGITUDE"].get<std::string>() == "-102.28") {
                found = true;
                break;
            }
        }

        REQUIRE(found);
    }
}

TEST_CASE("DataFrame: filename + options + format", "[data_frame]") {
    DataFrameOptions options;
    options.set_key_column("A")
        .set_duplicate_key_policy(DataFrameOptions::DuplicateKeyPolicy::OVERWRITE);

    CSVFormat format;
    format.delimiter(',').header_row(0);

    DataFrame frame(
        "./tests/data/fake_data/ints_squared.csv",
        options,
        format
    );

    REQUIRE(frame.size() == 100);
    REQUIRE(frame.contains("1"));
    REQUIRE(frame.contains("100"));
    REQUIRE(frame["50"]["B"].get<std::string>() == "2500");
}

TEST_CASE("DataFrame: options validation", "[data_frame]") {
    SECTION("Empty key column") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrameOptions options;

        REQUIRE_THROWS_AS(DataFrame(reader, options), std::runtime_error);
    }

    SECTION("Missing key column") {
        auto input = make_people_stream();
        CSVReader reader(input);
        DataFrameOptions options;
        options.set_key_column("missing");

        REQUIRE_THROWS_AS(DataFrame(reader, options), std::runtime_error);
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
        DataFrame frame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::KEEP_FIRST);

        REQUIRE(frame.size() == 2);
        REQUIRE(frame["1"]["name"].get<std::string>() == "Alice");
        REQUIRE(frame["1"]["value"].get<std::string>() == "10");
    }

    SECTION("THROW") {
        auto input = make_people_stream();
        CSVReader reader(input);

        REQUIRE_THROWS_AS(
            DataFrame(reader, "id", DataFrameOptions::DuplicateKeyPolicy::THROW),
            std::runtime_error
        );
    }
}

TEST_CASE("DataFrame: edit overlay and column extraction", "[data_frame]") {
    auto input = make_people_stream();
    CSVReader reader(input);
    DataFrame frame(reader, "id");

    REQUIRE(frame.get("2", "name") == "Bob");

    frame.set("2", "name", "Bobby");
    frame.set("1", "value", "31");

    REQUIRE(frame.get("2", "name") == "Bobby");
    REQUIRE(frame.get("1", "value") == "31");

    auto names = frame.column("name");
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "Carol");
    REQUIRE(names[1] == "Bobby");

    REQUIRE(frame.erase_row("2"));
    REQUIRE_FALSE(frame.contains("2"));
    REQUIRE(frame.size() == 1);

    REQUIRE_THROWS_AS(frame.column("missing"), std::runtime_error);
}
