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
                return owner->make_cell(row_index, false);
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
#if CSV_EXCEPTIONS_ENABLED
        const std::string& name() const {
            CSVError error;
            this->require_valid(error);
            internals::throw_dataframe_error(error);
            return frame_->columns()[col_index_];
        }
#endif

        /** Checked cell access, preserving mutability of the column proxy. */
        DataFrameCell try_field(size_t row_index, CSVError& error) {
            if (!this->require_valid(error)) return {};
            return frame_->try_make_cell(row_index, col_index_, mutable_frame_ != nullptr, error);
        }
        DataFrameCell try_field(size_t row_index, CSVError& error) const {
            if (!this->require_valid(error)) return {};
            return frame_->try_make_cell(row_index, col_index_, false, error);
        }
        csv::string_view try_get_sv(size_t row_index, CSVError& error) const {
            if (!this->require_valid(error)) return {};
            const auto binding = frame_->storage_.try_bind(row_index, col_index_, error);
            (void)binding;
            return error ? csv::string_view() : frame_->cell_view(row_index, col_index_);
        }
        csv::string_view try_name(CSVError& error) const {
            if (!this->require_valid(error)) return {};
            return csv::string_view(frame_->columns()[col_index_]);
        }
        template<typename T = std::string>
        std::vector<T> try_to_vector(CSVError& error) const {
            std::vector<T> values;
            if (!this->require_valid(error)) return values;
            values.reserve(this->size());
            for (size_t i = 0; i < this->size(); ++i) {
                auto cell = this->try_field(i, error);
                if (error) return values;
                T value{};
                CSVConversionError conversion;
                if (!cell.try_get(value, conversion)) {
                    internals::fail(error, CSVErrorCode::ConversionFailed, csv_conversion_error_message(conversion));
                    error.conversion = conversion;
                    return values;
                }
                values.push_back(std::move(value));
            }
            return values;
        }
        bool try_erase(CSVError& error) {
            if (!this->require_valid(error)) return false;
            if (!mutable_frame_)
                return internals::fail(error, CSVErrorCode::ReadOnlyProxy, "cannot erase column from const DataFrame");
            return mutable_frame_->erase_column_at_index(col_index_, error);
        }

        /** Whether this column still belongs to the active schema generation. */
        bool is_valid() const noexcept {
            return generation_ && *generation_ == generation_value_;
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

        /** Access a visible cell value by row index, allowing edits on mutable frames. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameCell operator[](size_t row_index) {
            CSVError error;
            auto result = this->try_field(row_index, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Access a visible cell value by row index without allowing edits. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameCell operator[](size_t row_index) const {
            CSVError error;
            auto result = this->try_field(row_index, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Access a visible cell value as a string_view without materializing a DataFrameCell.
         *
         *  Intended for immediate read-only scans. If the value comes from the
         *  sparse edit overlay, the returned view points into overlay-owned
         *  storage and must not be retained across DataFrame mutation.
         */
#if CSV_EXCEPTIONS_ENABLED
        csv::string_view get_sv(size_t row_index) const {
            CSVError error;
            auto result = this->try_get_sv(row_index, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Materialize this column as a vector of converted values. */
#if CSV_EXCEPTIONS_ENABLED
        template<typename T = std::string>
        std::vector<T> to_vector() const {
            CSVError error;
            auto result = this->template try_to_vector<T>(error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Convert to a vector of strings. */
#if CSV_EXCEPTIONS_ENABLED
        operator std::vector<std::string>() const {
            std::vector<std::string> result;
            result.reserve(this->size());
            for (size_t i = 0; i < this->size(); ++i)
                result.emplace_back(this->make_cell(i, false).get_sv());
            return result;
        }
#endif

        /** Delete this column from the parent DataFrame.
         *
         *  Structural mutation invalidates outstanding row, column, and cell proxies.
         */
#if CSV_EXCEPTIONS_ENABLED
        bool erase() {
            CSVError error;
            const bool result = this->try_erase(error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

#if defined(CSV_HAS_CXX20) && CSV_EXCEPTIONS_ENABLED
        /** Convert this DataFrameColumn into a std::ranges::input_range of strings.
         *
         *  @note Requires C++20 or later.
         */
        auto to_sv_range() const {
            return std::views::iota(size_t{0}, this->size())
                | std::views::transform([this](size_t row_index) {
                    return std::string(this->make_cell(row_index, false).get_sv());
                });
        }
#endif

#if CSV_EXCEPTIONS_ENABLED
        /** Iterate over visible cells in this column. */
        iterator begin() const { return iterator(this, 0); }
        iterator end() const { return iterator(this, this->size()); }
        const_iterator cbegin() const { return const_iterator(this, 0); }
        const_iterator cend() const { return const_iterator(this, this->size()); }

#endif

    private:
        DataFrameCell make_cell(size_t row_index, bool mutable_access) const {
#if CSV_EXCEPTIONS_ENABLED
            CSVError error;
            if (!this->require_valid(error) || !frame_->validate_row_index(row_index, error))
                internals::throw_dataframe_error(error);
#else
            CSV_DEBUG_ASSERT(this->is_valid() && row_index < this->size());
#endif
            return frame_->make_cell(row_index, col_index_, mutable_access);
        }

        bool require_valid(CSVError& error) const {
            error.clear();
            return this->is_valid() || internals::fail(error, CSVErrorCode::InvalidProxy,
                "DataFrameColumn proxy is invalid after structural column mutation");
        }

        const DataFrame<KeyType>* frame_;
        DataFrame<KeyType>* mutable_frame_;
        size_t col_index_;
        std::shared_ptr<size_t> generation_;
        size_t generation_value_;
    };
}
