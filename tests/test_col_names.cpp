/** @file
 *  Unit tests for csv::internals::ColNames
 */

#include <catch2/catch_all.hpp>
#include "internal/col_names.hpp"

using namespace csv;
using namespace csv::internals;

// ============================================================
// Default (exact / case-sensitive) policy
// ============================================================

TEST_CASE("ColNames - empty on construction", "[col_names]") {
    ColNames cn;
    REQUIRE(cn.empty());
    REQUIRE(cn.size() == 0);
}

TEST_CASE("ColNames - set and get column names", "[col_names]") {
    ColNames cn;
    cn.set_col_names({"A", "B", "C"});

    REQUIRE(cn.size() == 3);
    REQUIRE_FALSE(cn.empty());

    auto names = cn.get_col_names();
    REQUIRE(names.size() == 3);
    REQUIRE(names[0] == "A");
    REQUIRE(names[1] == "B");
    REQUIRE(names[2] == "C");
}

TEST_CASE("ColNames - constructor with names", "[col_names]") {
    ColNames cn({"X", "Y", "Z"});
    REQUIRE(cn.size() == 3);
    REQUIRE(cn.get_col_names() == std::vector<std::string>{"X", "Y", "Z"});
}

TEST_CASE("ColNames - index_of (exact policy)", "[col_names]") {
    ColNames cn({"Name", "Age", "City"});

    REQUIRE(cn.index_of("Name") == 0);
    REQUIRE(cn.index_of("Age")  == 1);
    REQUIRE(cn.index_of("City") == 2);
}

TEST_CASE("ColNames - index_of returns CSV_NOT_FOUND for missing column (exact)", "[col_names]") {
    ColNames cn({"Name", "Age"});

    REQUIRE(cn.index_of("missing") == CSV_NOT_FOUND);
    REQUIRE(cn.index_of("")        == CSV_NOT_FOUND);
}

TEST_CASE("ColNames - exact policy is case-sensitive", "[col_names]") {
    ColNames cn({"Name", "Age"});

    // Different case must not match under EXACT policy
    REQUIRE(cn.index_of("name") == CSV_NOT_FOUND);
    REQUIRE(cn.index_of("NAME") == CSV_NOT_FOUND);
    REQUIRE(cn.index_of("AGE")  == CSV_NOT_FOUND);
}

TEST_CASE("ColNames - operator[] by index", "[col_names]") {
    ColNames cn({"First", "Second", "Third"});

    REQUIRE(cn[0] == "First");
    REQUIRE(cn[1] == "Second");
    REQUIRE(cn[2] == "Third");
}

TEST_CASE("ColNames - operator[] throws on out-of-bounds", "[col_names]") {
    ColNames cn({"A", "B"});
    REQUIRE_THROWS_AS(cn[2], std::out_of_range);
    REQUIRE_THROWS_AS(cn[100], std::out_of_range);
}

TEST_CASE("ColNames - operator[] throws on empty ColNames", "[col_names]") {
    ColNames cn;
    REQUIRE_THROWS_AS(cn[0], std::out_of_range);
}

TEST_CASE("ColNames - set_col_names replaces existing names", "[col_names]") {
    ColNames cn({"Old1", "Old2"});
    cn.set_col_names({"New1", "New2", "New3"});

    REQUIRE(cn.size() == 3);
    REQUIRE(cn.index_of("New1")  == 0);
    REQUIRE(cn.index_of("Old1")  == CSV_NOT_FOUND);
}

// ============================================================
// Case-insensitive policy
// ============================================================

TEST_CASE("ColNames - case-insensitive index_of: lowercase query", "[col_names][case_insensitive]") {
    ColNames cn;
    cn.set_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    cn.set_col_names({"Name", "Age", "City"});

    REQUIRE(cn.index_of("name") == 0);
    REQUIRE(cn.index_of("age")  == 1);
    REQUIRE(cn.index_of("city") == 2);
}

TEST_CASE("ColNames - case-insensitive index_of: uppercase query", "[col_names][case_insensitive]") {
    ColNames cn;
    cn.set_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    cn.set_col_names({"Name", "Age", "City"});

    REQUIRE(cn.index_of("NAME") == 0);
    REQUIRE(cn.index_of("AGE")  == 1);
    REQUIRE(cn.index_of("CITY") == 2);
}

TEST_CASE("ColNames - case-insensitive index_of: exact query still works", "[col_names][case_insensitive]") {
    ColNames cn;
    cn.set_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    cn.set_col_names({"Name", "Age", "City"});

    REQUIRE(cn.index_of("Name") == 0);
    REQUIRE(cn.index_of("Age")  == 1);
    REQUIRE(cn.index_of("City") == 2);
}

TEST_CASE("ColNames - case-insensitive missing column returns CSV_NOT_FOUND", "[col_names][case_insensitive]") {
    ColNames cn;
    cn.set_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    cn.set_col_names({"Name", "Age"});

    REQUIRE(cn.index_of("missing") == CSV_NOT_FOUND);
    REQUIRE(cn.index_of("")        == CSV_NOT_FOUND);
}

TEST_CASE("ColNames - case-insensitive get_col_names preserves original casing", "[col_names][case_insensitive]") {
    // The stored names should be in their original form even under CI policy.
    // The lowercase transform is internal to the lookup map only.
    ColNames cn;
    cn.set_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    cn.set_col_names({"ReportDt", "Unit", "Power"});

    auto names = cn.get_col_names();
    REQUIRE(names[0] == "ReportDt");
    REQUIRE(names[1] == "Unit");
    REQUIRE(names[2] == "Power");
}

TEST_CASE("ColNames - case-insensitive operator[] preserves original casing", "[col_names][case_insensitive]") {
    ColNames cn;
    cn.set_policy(ColumnNamePolicy::CASE_INSENSITIVE);
    cn.set_col_names({"ReportDt", "Unit"});

    REQUIRE(cn[0] == "ReportDt");
    REQUIRE(cn[1] == "Unit");
}

TEST_CASE("ColNames - policy must be set before set_col_names to take effect", "[col_names][case_insensitive]") {
    // set_col_names called BEFORE set_policy: map is built with exact keys,
    // so CI lookup will not work.
    ColNames cn({"Name", "Age"});  // policy is EXACT at this point
    cn.set_policy(ColumnNamePolicy::CASE_INSENSITIVE);

    // The map was built with exact keys so lowercase query won't find anything.
    REQUIRE(cn.index_of("name") == CSV_NOT_FOUND);

    // After rebuilding the map, CI works.
    cn.set_col_names(cn.get_col_names());
    REQUIRE(cn.index_of("name") == 0);
}

// ============================================================
// Edge cases
// ============================================================

TEST_CASE("ColNames - empty column name list", "[col_names][edge_cases]") {
    ColNames cn;
    cn.set_col_names({});

    REQUIRE(cn.empty());
    REQUIRE(cn.size() == 0);
    REQUIRE(cn.index_of("anything") == CSV_NOT_FOUND);
}

TEST_CASE("ColNames - column name that is an empty string", "[col_names][edge_cases]") {
    ColNames cn({"", "B", "C"});
    REQUIRE(cn.index_of("") == 0);
    REQUIRE(cn.index_of("B") == 1);
}

TEST_CASE("ColNames - duplicate column names: last index wins", "[col_names][edge_cases]") {
    // When header has duplicate names the last occurrence wins in the hash map.
    // This documents current behavior rather than prescribing it.
    ColNames cn({"dup", "other", "dup"});
    REQUIRE(cn.index_of("dup") == 2);
    REQUIRE(cn.index_of("other") == 1);
}
