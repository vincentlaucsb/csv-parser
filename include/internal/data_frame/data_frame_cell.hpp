#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "../csv_exceptions.hpp"
#include "../csv_row.hpp"
#include "row_overlay.hpp"

namespace csv {
    class DataFrameCell : public CSVField {
    public:
        using CSVField::get;
        using CSVField::get_sv;
        using CSVField::is_float;
        using CSVField::is_int;
        using CSVField::is_null;
        using CSVField::is_num;
        using CSVField::is_str;
        using CSVField::try_get;
        using CSVField::type;

        DataFrameCell() : CSVField(csv::string_view()), row(nullptr), row_overlay(nullptr), col_index(0), can_mutate(false) {}

        DataFrameCell(const DataFrameCell& other)
            : CSVField(csv::string_view()),
            row(other.row),
            row_overlay(other.row_overlay),
            col_index(other.col_index),
            can_mutate(other.can_mutate),
            owned_value_(other.owned_value_) {
            this->refresh_value();
        }

        DataFrameCell(DataFrameCell&& other) noexcept
            : CSVField(csv::string_view()),
            row(other.row),
            row_overlay(other.row_overlay),
            col_index(other.col_index),
            can_mutate(other.can_mutate),
            owned_value_(std::move(other.owned_value_)) {
            this->refresh_value();
        }

        DataFrameCell(
            const CSVRow* _row,
            RowOverlay* _row_overlay,
            size_t _col_index
        ) : CSVField(csv::string_view()),
            row(_row),
            row_overlay(_row_overlay),
            col_index(_col_index),
            can_mutate(true) {
            this->refresh_value();
        }

        DataFrameCell(
            const CSVRow* _row,
            const RowOverlay* _row_overlay,
            size_t _col_index
        ) : CSVField(csv::string_view()),
            row(_row),
            row_overlay(_row_overlay),
            col_index(_col_index),
            can_mutate(false) {
            this->refresh_value();
        }

        DataFrameCell& operator=(const DataFrameCell& other) {
            if (this != &other) {
                row = other.row;
                row_overlay = other.row_overlay;
                col_index = other.col_index;
                can_mutate = other.can_mutate;
                owned_value_ = other.owned_value_;
                this->refresh_value();
            }

            return *this;
        }

        DataFrameCell& operator=(DataFrameCell&& other) noexcept {
            if (this != &other) {
                row = other.row;
                row_overlay = other.row_overlay;
                col_index = other.col_index;
                can_mutate = other.can_mutate;
                owned_value_ = std::move(other.owned_value_);
                this->refresh_value();
            }

            return *this;
        }

        DataFrameCell& operator=(csv::string_view value) {
            return this->assign(std::string(value));
        }

        /** Const-friendly read access for proxy use in column iteration and callbacks. */
        template<typename T = std::string>
        T get() const {
            return const_cast<DataFrameCell*>(this)->CSVField::template get<T>();
        }

        bool is_null() const noexcept {
            return const_cast<DataFrameCell*>(this)->CSVField::is_null();
        }

        bool is_str() const noexcept {
            return const_cast<DataFrameCell*>(this)->CSVField::is_str();
        }

        bool is_num() const noexcept {
            return const_cast<DataFrameCell*>(this)->CSVField::is_num();
        }

        bool is_int() const noexcept {
            return const_cast<DataFrameCell*>(this)->CSVField::is_int();
        }

        bool is_float() const noexcept {
            return const_cast<DataFrameCell*>(this)->CSVField::is_float();
        }

        DataType type() const noexcept {
            return const_cast<DataFrameCell*>(this)->CSVField::type();
        }

        template<typename T>
        bool try_get(T& out) const noexcept {
            return const_cast<DataFrameCell*>(this)->CSVField::template try_get<T>(out);
        }

    private:
        void refresh_value() {
            if (!row) {
                CSVField::operator=(CSVField(csv::string_view()));
                return;
            }

            if (row_overlay && row_overlay->try_get_copy(col_index, owned_value_)) {
                CSVField::operator=(CSVField(csv::string_view(owned_value_)));
                return;
            }

            owned_value_.clear();
            CSVField::operator=(CSVField((*row)[col_index].template get<csv::string_view>()));
        }

        DataFrameCell& assign(std::string stored) {
            if (!can_mutate || !row_overlay) {
                throw std::runtime_error(internals::ERROR_CANNOT_EDIT_CONST_DF_CELL);
            }

            owned_value_ = stored;
            const_cast<RowOverlay*>(row_overlay)->set(col_index, std::move(stored));
            CSVField::operator=(CSVField(csv::string_view(owned_value_)));
            return *this;
        }

        const CSVRow* row;
        const RowOverlay* row_overlay;
        size_t col_index;
        bool can_mutate;
        std::string owned_value_;
    };
}
