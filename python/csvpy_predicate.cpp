#include "csvpy_predicate.hpp"

#include <cctype>

namespace {
    char ascii_lower(char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    }
}

RowPredicate::RowPredicate(std::string column, std::string value, bool case_sensitive)
    : column_(std::move(column)),
      value_(std::move(value)),
      case_sensitive_(case_sensitive) {}

const std::string& RowPredicate::column() const noexcept {
    return this->column_;
}

const std::string& RowPredicate::value() const noexcept {
    return this->value_;
}

bool RowPredicate::case_sensitive() const noexcept {
    return this->case_sensitive_;
}

bool RowPredicate::matches(csv::string_view candidate) const {
    if (candidate.size() != this->value_.size()) {
        return false;
    }

    for (size_t i = 0; i < candidate.size(); ++i) {
        char left = candidate[i];
        char right = this->value_[i];
        if (!this->case_sensitive_) {
            left = ascii_lower(left);
            right = ascii_lower(right);
        }

        if (left != right) {
            return false;
        }
    }

    return true;
}

size_t RowPredicate::column_index(const std::vector<std::string>& columns) const {
    const auto it = std::find(columns.begin(), columns.end(), this->column_);
    if (it == columns.end()) {
        throw std::runtime_error("predicate column not found: " + this->column_);
    }

    return static_cast<size_t>(it - columns.begin());
}

const RowPredicate* optional_row_predicate(nb::object predicate) {
    if (predicate.is_none()) {
        return nullptr;
    }

    if (!nb::isinstance<RowPredicate>(predicate)) {
        throw nb::type_error("predicate must be created by csvpy.equal()");
    }

    return nb::cast<RowPredicate*>(predicate);
}

std::vector<std::uint8_t> excluded_rows_for_predicate(
    const DataFrame<>& frame,
    const std::vector<std::uint8_t>& deleted_rows,
    const RowPredicate* predicate
) {
    std::vector<std::uint8_t> excluded(frame.size(), 0);
    for (size_t row_index = 0; row_index < frame.size(); ++row_index) {
        if (row_index < deleted_rows.size() && deleted_rows[row_index]) {
            excluded[row_index] = 1;
        }
    }

    if (!predicate) {
        return excluded;
    }

    const size_t column_index = predicate->column_index(frame.columns());
    const auto column = frame.column_view(column_index);
    for (size_t row_index = 0; row_index < frame.size(); ++row_index) {
        if (!excluded[row_index] && !predicate->matches(column.get_sv(row_index))) {
            excluded[row_index] = 1;
        }
    }

    return excluded;
}

size_t mark_matching_rows(
    const DataFrame<>& frame,
    std::vector<std::uint8_t>& deleted_rows,
    size_t& pending_delete_count,
    const RowPredicate& predicate
) {
    if (deleted_rows.size() < frame.size()) {
        deleted_rows.resize(frame.size(), 0);
    }

    const size_t column_index = predicate.column_index(frame.columns());
    const auto column = frame.column_view(column_index);
    size_t marked = 0;
    for (size_t row_index = 0; row_index < frame.size(); ++row_index) {
        if (!deleted_rows[row_index] && predicate.matches(column.get_sv(row_index))) {
            deleted_rows[row_index] = 1;
            ++pending_delete_count;
            ++marked;
        }
    }

    return marked;
}

void init_CSVPredicate(nb::module_& m) {
    nb::class_<RowPredicate>(m, "_Predicate")
    .def_prop_ro("column", &RowPredicate::column)
    .def_prop_ro("value", &RowPredicate::value)
    .def_prop_ro("case_sensitive", &RowPredicate::case_sensitive);

    m.def(
        "equal",
        [](std::string column, std::string value, bool case_sensitive) {
            return RowPredicate(std::move(column), std::move(value), case_sensitive);
        },
        "Create a native equality predicate for row filtering.",
        nb::arg("column"),
        nb::arg("value"),
        nb::arg("case_sensitive") = true
    );
}
