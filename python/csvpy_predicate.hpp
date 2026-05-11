#pragma once

#include "csvpy_bindings.hpp"

enum class RowPredicateOp {
    EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL
};

class RowPredicate {
public:
    RowPredicate(std::string column, std::string value, bool case_sensitive = true);
    RowPredicate(std::string column, std::string value, RowPredicateOp op, bool case_sensitive = true);
    explicit RowPredicate(std::vector<RowPredicate> predicates);

    const std::string& column() const noexcept;
    const std::string& value() const noexcept;
    bool case_sensitive() const noexcept;
    RowPredicateOp op() const noexcept;
    bool is_all_of() const noexcept;
    const std::vector<RowPredicate>& children() const noexcept;
    bool matches(csv::string_view candidate) const;
    size_t column_index(const std::vector<std::string>& columns) const;
    void validate_columns(const std::vector<std::string>& columns) const;

private:
    std::string column_;
    std::string value_;
    RowPredicateOp op_ = RowPredicateOp::EQUAL;
    bool case_sensitive_ = true;
    long double numeric_value_ = 0;
    bool has_numeric_value_ = false;
    std::vector<RowPredicate> children_;
};

const RowPredicate* optional_row_predicate(nb::object predicate);
std::vector<std::uint8_t> excluded_rows_for_predicate(
    const DataFrame<>& frame,
    const std::vector<std::uint8_t>& deleted_rows,
    const RowPredicate* predicate
);
std::vector<std::uint8_t> included_rows_for_predicate(
    const DataFrame<>& frame,
    const std::vector<std::uint8_t>& deleted_rows,
    const RowPredicate& predicate
);
size_t mark_matching_rows(
    const DataFrame<>& frame,
    std::vector<std::uint8_t>& deleted_rows,
    size_t& pending_delete_count,
    const RowPredicate& predicate
);

void init_CSVPredicate(nb::module_& m);
