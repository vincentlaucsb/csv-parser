#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "data_frame_cell.hpp"
#include "fwd.hpp"
#include "indexed_proxy_iterator.hpp"

namespace csv {
    template<typename KeyType>
    class DataFrameColumn {
    public:
        struct cell_accessor {
            DataFrameCell operator()(const DataFrameColumn<KeyType>* owner, size_t row_index) const {
                return owner->operator[](row_index);
            }
        };

        using iterator = internals::indexed_proxy_iterator<const DataFrameColumn<KeyType>, DataFrameCell, cell_accessor>;
        using const_iterator = iterator;

        DataFrameColumn() : frame_(nullptr), mutable_frame_(nullptr), col_index_(0), generation_value_(0) {}

        DataFrameColumn(const DataFrame<KeyType>* frame, size_t col_index)
            : frame_(frame),
            mutable_frame_(nullptr),
            col_index_(col_index),
            generation_(frame ? frame->column_generation_ : std::shared_ptr<size_t>()),
            generation_value_(generation_ ? *generation_ : 0) {}

        DataFrameColumn(DataFrame<KeyType>* frame, size_t col_index)
            : frame_(frame),
            mutable_frame_(frame),
            col_index_(col_index),
            generation_(frame ? frame->column_generation_ : std::shared_ptr<size_t>()),
            generation_value_(generation_ ? *generation_ : 0) {}

        /** Column name. */
        const std::string& name() const {
            this->require_valid();
            return (*frame_->col_names_)[col_index_];
        }

        /** Zero-based column position. */
        size_t index() const noexcept {
            return col_index_;
        }

        /** Number of rows in the parent batch. */
        size_t size() const noexcept {
            if (!this->is_valid()) {
                return 0;
            }

            return frame_->size();
        }

        /** Whether the parent batch contains no rows. */
        bool empty() const noexcept {
            return this->size() == 0;
        }

        /** Access a visible cell value by row index. */
        DataFrameCell operator[](size_t row_index) const {
            this->require_valid();
            const auto& row = frame_->rows.at(row_index);
            const auto* row_edits = frame_->find_row_edits(row_index);
            return DataFrameCell(&row, row_edits, frame_->physical_column_index(col_index_));
        }

        /** Access a visible cell value as a string_view without materializing a DataFrameCell.
         *
         *  Intended for immediate read-only scans. If the value comes from the
         *  sparse edit overlay, the returned view points into overlay-owned
         *  storage and must not be retained across DataFrame mutation.
         */
        csv::string_view get_sv(size_t row_index) const {
            this->require_valid();
            const auto& row = frame_->rows.at(row_index);
            const auto* row_edits = frame_->find_row_edits(row_index);
            const size_t physical_index = frame_->physical_column_index(col_index_);
            csv::string_view edited_value;
            if (row_edits && row_edits->try_get_view(physical_index, edited_value)) {
                return edited_value;
            }

            return row[physical_index].template get<csv::string_view>();
        }

        /** Materialize this column as a vector of converted values. */
        template<typename T = std::string>
        std::vector<T> to_vector() const {
            std::vector<T> values;
            values.reserve(this->size());

            for (size_t row_index = 0; row_index < this->size(); ++row_index) {
                values.push_back((*this)[row_index].template get<T>());
            }

            return values;
        }

        /** Convert to a vector of strings. */
        operator std::vector<std::string>() const {
            return this->to_vector<std::string>();
        }

        /** Delete this column from the parent DataFrame.
         *
         *  Structural mutation invalidates outstanding row, column, and cell proxies.
         */
        bool erase() {
            if (!mutable_frame_) {
                throw std::runtime_error("cannot erase column from const DataFrame");
            }

            return mutable_frame_->erase_column_at_index(col_index_);
        }

#ifdef CSV_HAS_CXX20
        /** Convert this DataFrameColumn into a std::ranges::input_range of strings.
         *
         *  @note Requires C++20 or later.
         */
        auto to_sv_range() const {
            return std::views::iota(size_t{0}, this->size())
                | std::views::transform([this](size_t row_index) {
                    return (*this)[row_index].template get<std::string>();
                });
        }
#endif

        /** Iterate over visible cells in this column. */
        iterator begin() const { return iterator(this, 0); }
        iterator end() const { return iterator(this, this->size()); }
        const_iterator cbegin() const { return const_iterator(this, 0); }
        const_iterator cend() const { return const_iterator(this, this->size()); }

    private:
        bool is_valid() const noexcept {
            return generation_ && *generation_ == generation_value_;
        }

        void require_valid() const {
            if (!this->is_valid()) {
                throw std::runtime_error("DataFrameColumn proxy is invalid after structural column mutation");
            }
        }

        const DataFrame<KeyType>* frame_;
        DataFrame<KeyType>* mutable_frame_;
        size_t col_index_;
        std::shared_ptr<size_t> generation_;
        size_t generation_value_;
    };
}
