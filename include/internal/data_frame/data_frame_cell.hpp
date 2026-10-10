#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "../csv_exceptions.hpp"
#include "../csv_row.hpp"
#include "dirty_data_frame.hpp"
#include "fwd.hpp"
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

        DataFrameCell() : CSVField(csv::string_view()), row_(nullptr), edit_context_(nullptr), col_index_(0), can_mutate_(false) {}

        DataFrameCell(const DataFrameCell& other)
            : CSVField(csv::string_view()),
            row_(other.row_),
            edit_context_(other.edit_context_),
            col_index_(other.col_index_),
            can_mutate_(other.can_mutate_),
            frame_bound_(other.frame_bound_),
            owned_value_(other.owned_value_) {
            this->refresh_value();
        }

        DataFrameCell(DataFrameCell&& other) noexcept
            : CSVField(csv::string_view()),
            row_(other.row_),
            edit_context_(other.edit_context_),
            col_index_(other.col_index_),
            can_mutate_(other.can_mutate_),
            frame_bound_(other.frame_bound_),
            owned_value_(std::move(other.owned_value_)) {
            this->refresh_value();
        }

        DataFrameCell(
            const CSVRow* _row,
            RowOverlay* _row_overlay,
            size_t _col_index
        ) : CSVField(csv::string_view()),
            row_(_row),
            edit_context_(_row_overlay),
            col_index_(_col_index),
            can_mutate_(true) {
            this->refresh_value(_row_overlay);
        }

        DataFrameCell(
            const CSVRow* _row,
            const RowOverlay* _row_overlay,
            size_t _col_index
        ) : CSVField(csv::string_view()),
            row_(_row),
            edit_context_(_row_overlay),
            col_index_(_col_index),
            can_mutate_(false) {
            this->refresh_value(_row_overlay);
        }

        DataFrameCell& operator=(const DataFrameCell& other) {
            if (this != &other) {
                row_ = other.row_;
                edit_context_ = other.edit_context_;
                col_index_ = other.col_index_;
                can_mutate_ = other.can_mutate_;
                owned_value_ = other.owned_value_;
                frame_bound_ = other.frame_bound_;
                this->refresh_value();
            }

            return *this;
        }

        DataFrameCell& operator=(DataFrameCell&& other) noexcept {
            if (this != &other) {
                row_ = other.row_;
                edit_context_ = other.edit_context_;
                col_index_ = other.col_index_;
                can_mutate_ = other.can_mutate_;
                owned_value_ = std::move(other.owned_value_);
                frame_bound_ = other.frame_bound_;
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
        template<typename KeyType> friend class DataFrame;

        // Frame-bound cells can defer overlay allocation until assignment. Cell
        // reads retain the existing snapshot semantics; row proxies resolve afresh.
        DataFrameCell(
            const CSVRow* source, const RowOverlay* overlay, size_t column,
            const internals::data_frame::DirtyDataFrame* dirty_state,
            bool mutable_access
        ) : CSVField(csv::string_view()), row_(source),
            edit_context_(mutable_access ? static_cast<const void*>(dirty_state) : static_cast<const void*>(overlay)),
            col_index_(column), can_mutate_(mutable_access), frame_bound_(mutable_access) {
            this->refresh_value(overlay);
        }

        const internals::data_frame::DirtyDataFrame* dirty_state() const {
            return static_cast<const internals::data_frame::DirtyDataFrame*>(edit_context_);
        }

        const RowOverlay* find_overlay() const {
            if (frame_bound_) {
                const auto* state = this->dirty_state();
                return state->is_dirty() ? state->find_row_edits(state->row_index(row_)) : nullptr;
            }
            return static_cast<const RowOverlay*>(edit_context_);
        }

        void refresh_value() {
            this->refresh_value(this->find_overlay());
        }

        void refresh_value(const RowOverlay* overlay) {
            if (!row_) {
                CSVField::operator=(CSVField(csv::string_view()));
                return;
            }

            if (overlay && overlay->try_get_copy(col_index_, owned_value_)) {
                CSVField::operator=(CSVField(csv::string_view(owned_value_)));
                return;
            }

            owned_value_.clear();
            CSVField::operator=(CSVField((*row_)[col_index_].template get<csv::string_view>()));
        }

        DataFrameCell& assign(std::string stored) {
            if (!can_mutate_ || !edit_context_) {
                throw std::runtime_error(internals::ERROR_CANNOT_EDIT_CONST_DF_CELL);
            }

            owned_value_ = stored;
            auto* state = frame_bound_
                ? const_cast<internals::data_frame::DirtyDataFrame*>(this->dirty_state()) : nullptr;
            RowOverlay* overlay = state ? state->ensure_row_edits(state->row_index(row_))
                : const_cast<RowOverlay*>(static_cast<const RowOverlay*>(edit_context_));
            overlay->set(col_index_, std::move(stored));
            CSVField::operator=(CSVField(csv::string_view(owned_value_)));
            return *this;
        }

        const CSVRow* row_;
        // Tagged context: standalone/read-only cells hold an overlay; mutable
        // frame-bound cells hold the lazy handler. This uses existing padding
        // for the tag instead of adding an owner pointer and row index per cell.
        const void* edit_context_;
        size_t col_index_;
        bool can_mutate_;
        bool frame_bound_ = false;
        std::string owned_value_;
    };
}
