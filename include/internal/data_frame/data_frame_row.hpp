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
        DataFrameCell operator[](const std::string& col) {
            return this->make_cell(this->find_column(col));
        }

        /** Access a field by position, preserving edit support. */
        DataFrameCell operator[](size_t n) {
            return this->make_cell(n);
        }

        /** Access a field by column name, checking edits first. */
        DataFrameCell operator[](const std::string& col) const {
            return this->make_cell(this->find_column(col));
        }

        /** Access a field by position, checking edits first. */
        DataFrameCell operator[](size_t n) const {
            return this->make_cell(n);
        }

        /** Get the number of fields in the row. */
        size_t size() const { return row->size(); }

        /** Check if the row is empty. */
        bool empty() const { return row->empty(); }

        /** Get column names. */
        const std::vector<std::string>& get_col_names() const { return row->get_col_names(); }

        /** Get the underlying CSVRow for compatibility. */
        const CSVRow& get_underlying_row() const { return *row; }

        /** Get the key for this row (only valid for keyed DataFrames). */
        const KeyType& key() const { return *key_ptr; }

        /** Delete this row from the parent DataFrame.
         *
         *  Structural mutation invalidates outstanding row and cell proxies.
         */
        bool erase() {
            if (!can_mutate || !frame) {
                throw std::runtime_error(internals::ERROR_CANNOT_ERASE_CONST_DF_ROW);
            }

            return const_cast<DataFrame<KeyType>*>(frame)->erase_at_index(row_index);
        }

        /** Convert to vector of strings for CSVWriter compatibility. */
        operator std::vector<std::string>() const {
            std::vector<std::string> result;
            result.reserve(row->size());
            
            for (size_t i = 0; i < row->size(); i++) {
                result.push_back(this->make_cell(i).template get<std::string>());
            }
            return result;
        }

        /** Convert to JSON. */
        std::string to_json(const std::vector<std::string>& subset = {}) const {
            const field_string_accessor field_at(this);
            if (frame) {
                return this->get_frame_json_converter().row_to_json(this->size(), field_at, subset);
            }

            return this->make_detached_json_converter().row_to_json(this->size(), field_at, subset);
        }

        /** Convert to JSON array. */
        std::string to_json_array(const std::vector<std::string>& subset = {}) const {
            const field_string_accessor field_at(this);
            if (frame) {
                return this->get_frame_json_converter().row_to_json_array(this->size(), field_at, subset);
            }

            return this->make_detached_json_converter().row_to_json_array(this->size(), field_at, subset);
        }

#ifdef CSV_HAS_CXX20
        /** Convert this DataFrameRow into a std::ranges::input_range of strings,
         *  respecting the sparse overlay (edited values take precedence).
         *
         *  @note Requires C++20 or later.
         */
        auto to_sv_range() const {
            return std::views::iota(size_t{0}, this->size())
                | std::views::transform([this](size_t i) { return this->make_cell(i).template get<std::string>(); });
        }
#endif

    private:
        struct field_string_accessor {
            explicit field_string_accessor(const DataFrameRow* owner) : owner(owner) {}

            std::string operator()(size_t i) const {
                return owner->make_cell(i).template get<std::string>();
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
            return can_mutate
                ? DataFrameCell(row, const_cast<RowOverlay*>(row_overlay), col_index)
                : DataFrameCell(row, row_overlay, col_index);
        }

        DataFrameCell make_cell(size_t col_index) const {
            return DataFrameCell(row, row_overlay, col_index);
        }

        size_t find_column(const std::string& col) const {
            if (frame) {
                return frame->find_column(col);
            }

            const internals::ConstColNamesPtr col_names = row->col_names_ptr();
            const int position = col_names->index_of(col);
            if (position == CSV_NOT_FOUND) {
                internals::throw_column_not_found_out_of_range(col);
            }

            return static_cast<size_t>(position);
        }

        const CSVRow* row;
        const DataFrame<KeyType>* frame;
        size_t row_index;
        const RowOverlay* row_overlay;
        const KeyType* key_ptr;
        bool can_mutate;
    };

}
