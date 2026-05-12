#include "csvpy_predicate.hpp"

#include <cctype>
#include <cstdlib>

namespace {
    const size_t PREDICATE_PARALLEL_MIN_ROWS = 4096;

    struct PredicateClause {
        const RowPredicate* predicate;
        DataFrame<>::column_type column;
    };

    char ascii_lower(char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    }

    long double parse_predicate_numeric_value(const std::string& value) {
        long double parsed = 0;
        CSVField field(csv::string_view(value.data(), value.size()));
        if (!field.try_get(parsed)) {
            throw std::runtime_error("numeric predicate value is not numeric: " + value);
        }
        return parsed;
    }

    std::string python_scalar_to_string(nb::handle value) {
        auto unicode_to_string = [](PyObject* object) {
            Py_ssize_t size = 0;
            const char* data = PyUnicode_AsUTF8AndSize(object, &size);
            if (data == nullptr) {
                throw nb::python_error();
            }
            return std::string(data, static_cast<size_t>(size));
        };

        if (PyUnicode_Check(value.ptr())) {
            return unicode_to_string(value.ptr());
        }

        if (PyBool_Check(value.ptr())) {
            throw nb::type_error("predicate value must be a str, int, or float");
        }

        if (PyLong_Check(value.ptr()) || PyFloat_Check(value.ptr())) {
            PyObject* raw_text = PyObject_Str(value.ptr());
            if (raw_text == nullptr) {
                throw nb::python_error();
            }
            nb::object text = nb::steal<nb::object>(raw_text);
            return unicode_to_string(text.ptr());
        }

        throw nb::type_error("predicate value must be a str, int, or float");
    }

    bool predicate_parallel_enabled() {
        const char* value = std::getenv("CSVPY_PREDICATE_PARALLEL");
        return !value || std::string(value) != "0";
    }

    size_t predicate_worker_count(size_t row_count) {
#if CSV_ENABLE_THREADS
        if (!predicate_parallel_enabled() || row_count < PREDICATE_PARALLEL_MIN_ROWS) {
            return 0;
        }

        const unsigned int hardware_threads = std::thread::hardware_concurrency();
        if (hardware_threads == 0) {
            return 1;
        }

        return (std::min)(static_cast<size_t>(hardware_threads), row_count / PREDICATE_PARALLEL_MIN_ROWS);
#else
        (void)row_count;
        return 0;
#endif
    }

    size_t predicate_chunk_count(size_t row_count, size_t worker_count) {
        if (row_count == 0) {
            return 0;
        }
        if (worker_count == 0) {
            return 1;
        }

        return (std::min)(row_count, worker_count * 4);
    }

    void append_predicate_clauses(
        const DataFrame<>& frame,
        const RowPredicate& predicate,
        std::vector<PredicateClause>& clauses
    ) {
        if (predicate.is_all_of()) {
            for (const auto& child : predicate.children()) {
                append_predicate_clauses(frame, child, clauses);
            }
            return;
        }

        const size_t column_index = predicate.column_index(frame.columns());
        clauses.push_back(PredicateClause { &predicate, frame.column_view(column_index) });
    }

    bool predicate_matches_row(const std::vector<PredicateClause>& clauses, size_t row_index) {
        for (const auto& clause : clauses) {
            if (!clause.predicate->matches(clause.column.get_sv(row_index))) {
                return false;
            }
        }

        return true;
    }

    void evaluate_predicate_matches(
        const DataFrame<>& frame,
        const RowPredicate& predicate,
        std::vector<std::uint8_t>& matches
    ) {
        matches.assign(frame.size(), 0);
        if (frame.empty()) {
            return;
        }

        std::vector<PredicateClause> clauses;
        append_predicate_clauses(frame, predicate, clauses);
        const size_t worker_count = predicate_worker_count(frame.size());
        const size_t chunk_count = predicate_chunk_count(frame.size(), worker_count);
        DataFrameExecutor executor(worker_count);

        executor.parallel_for(chunk_count, [&clauses, &matches, chunk_count](size_t chunk_index) {
            const size_t row_count = matches.size();
            const size_t begin = (row_count * chunk_index) / chunk_count;
            const size_t end = (row_count * (chunk_index + 1)) / chunk_count;
            for (size_t row_index = begin; row_index < end; ++row_index) {
                matches[row_index] = predicate_matches_row(clauses, row_index) ? 1 : 0;
            }
        });
    }
}

RowPredicate::RowPredicate(std::string column, std::string value, bool case_sensitive)
    : RowPredicate(std::move(column), std::move(value), RowPredicateOp::EQUAL, case_sensitive) {}

RowPredicate::RowPredicate(std::string column, std::string value, RowPredicateOp op, bool case_sensitive)
    : column_(std::move(column)),
      value_(std::move(value)),
      op_(op),
      case_sensitive_(case_sensitive),
      has_numeric_value_(op != RowPredicateOp::EQUAL) {
    if (this->has_numeric_value_) {
        this->numeric_value_ = parse_predicate_numeric_value(this->value_);
    }
}

RowPredicate::RowPredicate(std::vector<RowPredicate> predicates)
    : children_(std::move(predicates)) {
    if (this->children_.empty()) {
        throw std::invalid_argument("all_of() requires at least one predicate");
    }
}

const std::string& RowPredicate::column() const noexcept {
    return this->column_;
}

const std::string& RowPredicate::value() const noexcept {
    return this->value_;
}

bool RowPredicate::case_sensitive() const noexcept {
    return this->case_sensitive_;
}

RowPredicateOp RowPredicate::op() const noexcept {
    return this->op_;
}

bool RowPredicate::is_all_of() const noexcept {
    return !this->children_.empty();
}

const std::vector<RowPredicate>& RowPredicate::children() const noexcept {
    return this->children_;
}

bool RowPredicate::matches(csv::string_view candidate) const {
    if (this->is_all_of()) {
        throw std::logic_error("compound predicates require row-aware evaluation");
    }

    if (this->op_ != RowPredicateOp::EQUAL) {
        long double candidate_value = 0;
        CSVField field(candidate);
        if (!field.try_get(candidate_value)) {
            return false;
        }

        switch (this->op_) {
        case RowPredicateOp::LESS:
            return candidate_value < this->numeric_value_;
        case RowPredicateOp::LESS_EQUAL:
            return candidate_value <= this->numeric_value_;
        case RowPredicateOp::GREATER:
            return candidate_value > this->numeric_value_;
        case RowPredicateOp::GREATER_EQUAL:
            return candidate_value >= this->numeric_value_;
        case RowPredicateOp::EQUAL:
            break;
        }
    }

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
    if (this->is_all_of()) {
        throw std::logic_error("compound predicates do not have a single column");
    }

    const auto it = std::find(columns.begin(), columns.end(), this->column_);
    if (it == columns.end()) {
        throw std::runtime_error("predicate column not found: " + this->column_);
    }

    return static_cast<size_t>(it - columns.begin());
}

void RowPredicate::validate_columns(const std::vector<std::string>& columns) const {
    if (this->is_all_of()) {
        for (const auto& child : this->children_) {
            child.validate_columns(columns);
        }
        return;
    }

    (void)this->column_index(columns);
}

const RowPredicate* optional_row_predicate(nb::object predicate) {
    if (predicate.is_none()) {
        return nullptr;
    }

    if (!nb::isinstance<RowPredicate>(predicate)) {
        throw nb::type_error("predicate must be created by a csvpy predicate factory");
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

    std::vector<std::uint8_t> matches;
    evaluate_predicate_matches(frame, *predicate, matches);
    for (size_t row_index = 0; row_index < frame.size(); ++row_index) {
        if (!excluded[row_index] && !matches[row_index]) {
            excluded[row_index] = 1;
        }
    }

    return excluded;
}

std::vector<std::uint8_t> included_rows_for_predicate(
    const DataFrame<>& frame,
    const std::vector<std::uint8_t>& deleted_rows,
    const RowPredicate& predicate
) {
    std::vector<std::uint8_t> matches;
    evaluate_predicate_matches(frame, predicate, matches);
    for (size_t row_index = 0; row_index < frame.size(); ++row_index) {
        if (row_index < deleted_rows.size() && deleted_rows[row_index]) {
            matches[row_index] = 0;
        }
    }
    return matches;
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

    std::vector<std::uint8_t> matches;
    evaluate_predicate_matches(frame, predicate, matches);
    size_t marked = 0;
    for (size_t row_index = 0; row_index < frame.size(); ++row_index) {
        if (!deleted_rows[row_index] && matches[row_index]) {
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
        [](std::string column, nb::object value, bool case_sensitive) {
            return RowPredicate(std::move(column), python_scalar_to_string(value), case_sensitive);
        },
        "Create a native equality predicate for row filtering.",
        nb::arg("column"),
        nb::arg("value").none(),
        nb::arg("case_sensitive") = true
    );
    m.def(
        "less",
        [](std::string column, nb::object value) {
            return RowPredicate(std::move(column), python_scalar_to_string(value), RowPredicateOp::LESS);
        },
        "Create a native numeric less-than predicate for row filtering.",
        nb::arg("column"),
        nb::arg("value").none()
    );
    m.def(
        "less_equal",
        [](std::string column, nb::object value) {
            return RowPredicate(std::move(column), python_scalar_to_string(value), RowPredicateOp::LESS_EQUAL);
        },
        "Create a native numeric less-than-or-equal predicate for row filtering.",
        nb::arg("column"),
        nb::arg("value").none()
    );
    m.def(
        "greater",
        [](std::string column, nb::object value) {
            return RowPredicate(std::move(column), python_scalar_to_string(value), RowPredicateOp::GREATER);
        },
        "Create a native numeric greater-than predicate for row filtering.",
        nb::arg("column"),
        nb::arg("value").none()
    );
    m.def(
        "greater_equal",
        [](std::string column, nb::object value) {
            return RowPredicate(std::move(column), python_scalar_to_string(value), RowPredicateOp::GREATER_EQUAL);
        },
        "Create a native numeric greater-than-or-equal predicate for row filtering.",
        nb::arg("column"),
        nb::arg("value").none()
    );
    m.def(
        "all_of",
        [](const nb::args& args) {
            if (args.size() == 0) {
                throw nb::type_error("all_of() requires at least one predicate");
            }

            std::vector<RowPredicate> predicates;
            predicates.reserve(args.size());
            for (nb::handle arg : args) {
                if (!nb::isinstance<RowPredicate>(arg)) {
                    throw nb::type_error("all_of() arguments must be predicates created by csvpy predicate factories");
                }

                predicates.push_back(*nb::cast<RowPredicate*>(arg));
            }

            return RowPredicate(std::move(predicates));
        },
        "Create a native conjunction predicate for row filtering."
    );
}
