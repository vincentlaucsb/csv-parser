#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "../csv_exceptions.hpp"
#include "../csv_row.hpp"
#include "../json_converter.hpp"
#include "data_frame_cell.hpp"
#include "fwd.hpp"

namespace csv {
    template<typename KeyType>
    class DataFrameRow {
    public:
        /** Default constructor (creates an unbound proxy). */
        DataFrameRow() : row(nullptr), frame(nullptr), row_index(0), row_overlay(nullptr), key_ptr(nullptr), can_mutate(false) {}

        /** Construct a mutable DataFrameRow wrapper. */
        DataFrameRow(
            const CSVRow* _row,
            DataFrame<KeyType>* _frame,
            size_t _row_index,
            RowOverlay* _edits,
            const KeyType* _key
        ) : row(_row), frame(_frame), row_index(_row_index), row_overlay(_edits), key_ptr(_key), can_mutate(true) {}

        /** Construct a read-only DataFrameRow wrapper. */
        DataFrameRow(
            const CSVRow* _row,
            const DataFrame<KeyType>* _frame,
            size_t _row_index,
            const RowOverlay* _edits,
            const KeyType* _key
        ) : row(_row), frame(_frame), row_index(_row_index), row_overlay(_edits), key_ptr(_key), can_mutate(false) {}

        /** Access a field by column name, preserving edit support. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameCell operator[](const std::string& col) {
            CSVError error;
            auto result = this->try_field(col, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Access a field by position, preserving edit support. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameCell operator[](size_t n) {
            CSVError error;
            auto result = this->try_field(n, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Access a field by column name, checking edits first. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameCell operator[](const std::string& col) const {
            CSVError error;
            auto result = this->try_field(col, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Access a field by position, checking edits first. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameCell operator[](size_t n) const {
            CSVError error;
            auto result = this->try_field(n, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Checked cell access. Failure returns an unbound, read-only cell. */
        DataFrameCell try_field(size_t index, CSVError& error) {
            if (!this->validate_field(index, error)) return {};
            return frame ? frame->try_make_cell(row_index, index, can_mutate, error) : this->make_cell(index);
        }
        DataFrameCell try_field(size_t index, CSVError& error) const {
            if (!this->validate_field(index, error)) return {};
            return frame ? frame->try_make_cell(row_index, index, false, error) : this->make_cell(index);
        }
        DataFrameCell try_field(const std::string& name, CSVError& error) {
            const size_t index = this->try_find_column(name, error);
            return error ? DataFrameCell() : this->try_field(index, error);
        }
        DataFrameCell try_field(const std::string& name, CSVError& error) const {
            const size_t index = this->try_find_column(name, error);
            return error ? DataFrameCell() : this->try_field(index, error);
        }

        bool try_erase(CSVError& error) {
            error.clear();
            if (!can_mutate || !frame)
                return internals::fail(error, CSVErrorCode::ReadOnlyProxy, internals::ERROR_CANNOT_ERASE_CONST_DF_ROW);
            return const_cast<DataFrame<KeyType>*>(frame)->erase_at_index(row_index);
        }

        /** Whether the proxy is bound. Structural mutation still invalidates proxies. */
        bool is_valid() const noexcept { return row != nullptr; }

        /** Get the number of visible fields in the row. */
        size_t size() const { return frame ? frame->n_cols() : (row ? row->size() : 0); }

        /** Check if the row is empty. */
        bool empty() const { return this->size() == 0; }

        /** Get column names. */
        const std::vector<std::string>& get_col_names() const { return frame ? frame->columns() : row->get_col_names(); }

        /** Get the underlying CSVRow without applying cell edits or column visibility.
         *  Use row conversion to obtain the current visible values.
         */
        const CSVRow& get_underlying_row() const { return *row; }

        /** Get the key for this row (only valid for keyed DataFrames). */
        const KeyType& key() const { return *key_ptr; }

        /** Delete this row from the parent DataFrame.
         *
         *  Structural mutation invalidates outstanding row and cell proxies.
         */
#if CSV_EXCEPTIONS_ENABLED
        bool erase() {
            CSVError error;
            const bool result = this->try_erase(error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Convert to vector of strings for CSVWriter compatibility. */
#if CSV_EXCEPTIONS_ENABLED
        operator std::vector<std::string>() const {
            std::vector<std::string> result;
            result.reserve(this->size());
            
            for (size_t i = 0; i < this->size(); i++) {
                result.push_back(std::string(this->make_cell(i).get_sv()));
            }
            return result;
        }
#endif

        /** Convert to JSON. */

#if CSV_EXCEPTIONS_ENABLED
        std::string to_json(const std::vector<std::string>& subset = {}) const {
            CSVError error;
            auto result = this->try_to_json(subset, error);
            // JSON subset lookups historically use runtime_error.
            internals::throw_error(error);
            return result;
        }
#endif

        /** Serialize selected visible columns, reporting lookup and physical-row bounds errors. */
        std::string try_to_json(const std::vector<std::string>& subset, CSVError& error) const {
            if (!this->validate_fields(subset, error)) return {};
            const field_string_accessor field_at(this);
            return frame ? this->get_frame_json_converter().try_row_to_json(this->size(), field_at, subset, error)
                : this->make_detached_json_converter().try_row_to_json(this->size(), field_at, subset, error);
        }

        /** Convert to JSON array. */

#if CSV_EXCEPTIONS_ENABLED
        std::string to_json_array(const std::vector<std::string>& subset = {}) const {
            CSVError error;
            auto result = this->try_to_json_array(subset, error);
            // JSON subset lookups historically use runtime_error.
            internals::throw_error(error);
            return result;
        }
#endif

        /** Serialize selected visible columns, reporting lookup and physical-row bounds errors. */
        std::string try_to_json_array(const std::vector<std::string>& subset, CSVError& error) const {
            if (!this->validate_fields(subset, error)) return {};
            const field_string_accessor field_at(this);
            return frame ? this->get_frame_json_converter().try_row_to_json_array(this->size(), field_at, subset, error)
                : this->make_detached_json_converter().try_row_to_json_array(this->size(), field_at, subset, error);
        }

#if defined(CSV_HAS_CXX20) && CSV_EXCEPTIONS_ENABLED
        /** Convert this DataFrameRow into a std::ranges::input_range of strings,
         *  respecting the sparse overlay (edited values take precedence).
         *
         *  @note Requires C++20 or later.
         */
        auto to_sv_range() const {
            return std::views::iota(size_t{0}, this->size())
                | std::views::transform([this](size_t i) { return std::string(this->make_cell(i).get_sv()); });
        }
#endif

    private:
        bool validate_field(size_t index, CSVError& error) const {
            error.clear();
            if (!row)
                return internals::fail(error, CSVErrorCode::InvalidProxy, "DataFrameRow proxy is unbound");
            if (index >= this->size())
                return internals::fail(error, CSVErrorCode::IndexOutOfBounds, internals::ERROR_COLUMN_INDEX_OUT_OF_RANGE);
            return true;
        }

        struct field_string_accessor {
            explicit field_string_accessor(const DataFrameRow* owner) : owner(owner) {}

            std::string operator()(size_t i) const {
                return std::string(owner->make_cell(i).get_sv());
            }

            const DataFrameRow* owner;
        };

        const internals::JsonConverter& get_frame_json_converter() const {
            return frame->json_converter_.get_or_create([this]() {
                return std::make_shared<internals::JsonConverter>(frame->columns());
            });
        }

        internals::JsonConverter make_detached_json_converter() const {
            return internals::JsonConverter(this->get_col_names());
        }

        DataFrameCell make_cell(size_t col_index) {
            if (frame) {
                return frame->make_cell(row, row_index, col_index, can_mutate);
            }

            return can_mutate
                ? DataFrameCell(row, const_cast<RowOverlay*>(row_overlay), col_index)
                : DataFrameCell(row, row_overlay, col_index);
        }

        DataFrameCell make_cell(size_t col_index) const {
            if (frame) {
                return frame->make_cell(row, row_index, col_index, false);
            }

            return DataFrameCell(row, row_overlay, col_index);
        }

        size_t try_find_column(const std::string& col, CSVError& error) const {
            error.clear();
            if (!row) {
                internals::fail(error, CSVErrorCode::InvalidProxy, "DataFrameRow proxy is unbound");
                return 0;
            }
            if (frame) return frame->try_find_column(col, error);
            const int position = row->col_names_ptr()->index_of(col);
            if (position == CSV_NOT_FOUND) {
                internals::fail(error, CSVErrorCode::ColumnNotFound, std::string(internals::ERROR_COLUMN_NOT_FOUND) + col);
                return 0;
            }
            return static_cast<size_t>(position);
        }

        bool validate_fields(const std::vector<std::string>& subset, CSVError& error) const {
            error.clear();
            if (subset.empty()) {
                for (size_t i = 0; i < this->size(); ++i) {
                    this->try_field(i, error);
                    if (error) return false;
                }
            } else {
                for (const auto& name : subset) {
                    this->try_field(name, error);
                    if (error) return false;
                }
            }
            return row || internals::fail(error, CSVErrorCode::InvalidProxy, "DataFrameRow proxy is unbound");
        }

        const CSVRow* row;
        const DataFrame<KeyType>* frame;
        size_t row_index;
        const RowOverlay* row_overlay;
        const KeyType* key_ptr;
        bool can_mutate;
    };

}
