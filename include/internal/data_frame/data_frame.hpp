#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../csv_exceptions.hpp"
#include "../csv_reader.hpp"
#include "../csv_writer.hpp"
#include "../json_converter.hpp"
#include "../raw_csv_data.hpp"
#include "data_frame_column.hpp"
#include "data_frame_executor.hpp"
#include "data_frame_options.hpp"
#include "data_frame_row.hpp"
#include "fwd.hpp"
#include "row_overlay.hpp"

namespace csv {
    template<typename KeyType = std::string>
    class DataFrame {
    public:
        friend class DataFrameRow<KeyType>;
        friend class DataFrameColumn<KeyType>;
        using row_type = DataFrameRow<KeyType>;
        using column_type = DataFrameColumn<KeyType>;

        struct mutable_row_accessor {
            DataFrameRow<KeyType> operator()(DataFrame<KeyType>* owner, size_t row_index) const {
                return owner->make_row_proxy(row_index);
            }
        };

        struct const_row_accessor {
            DataFrameRow<KeyType> operator()(const DataFrame<KeyType>* owner, size_t row_index) const {
                return owner->make_const_row_proxy(row_index);
            }
        };

        /** Row-wise iterator over DataFrameRow entries. Provides access to rows with edit support. */
        using iterator = internals::indexed_proxy_iterator<DataFrame<KeyType>, DataFrameRow<KeyType>, mutable_row_accessor>;

        /** Row-wise const iterator over DataFrameRow entries. Provides read-only access to rows with edit support. */
        using const_iterator = internals::indexed_proxy_iterator<const DataFrame<KeyType>, DataFrameRow<KeyType>, const_row_accessor>;

        static_assert(
            internals::is_hashable<KeyType>::value,
            "DataFrame<KeyType> requires KeyType to be hashable (std::hash<KeyType> specialization required)."
        );

        static_assert(
            internals::is_equality_comparable<KeyType>::value,
            "DataFrame<KeyType> requires KeyType to be equality comparable (operator== required)."
        );

        static_assert(
            std::is_default_constructible<KeyType>::value,
            "DataFrame<KeyType> requires KeyType to be default-constructible."
        );

        using DuplicateKeyPolicy = DataFrameOptions::DuplicateKeyPolicy;

        /** Construct an empty DataFrame. */
        DataFrame() = default;

        /**
         * Construct an unkeyed DataFrame from a CSV reader.
         * Rows are accessible by position only.
         */
        explicit DataFrame(CSVReader& reader) {
            this->init_unkeyed_from_reader(reader);
        }

        /** Construct an unkeyed DataFrame from an existing batch of rows. */
        explicit DataFrame(std::vector<CSVRow> rows) {
            this->init_unkeyed_from_rows(rows);
        }

        /** Construct a keyed DataFrame from a CSV reader with options.
         *
         * @throws std::runtime_error if key column is empty or not found
         */
        explicit DataFrame(CSVReader& reader, const DataFrameOptions& options) {
            this->init_from_reader(reader, options);
        }

        /** Construct a keyed DataFrame directly from a CSV file.
         *
         * @throws std::runtime_error if key column is empty or not found
         */
        DataFrame(
            csv::string_view filename,
            const DataFrameOptions& options,
            CSVFormat format = CSVFormat::guess_csv()
        ) {
            CSVReader reader(filename, format);
            this->init_from_reader(reader, options);
        }

        /** Construct a keyed DataFrame using a column name as the key.
         *
         * @throws std::runtime_error if key column is empty or not found
         */
        DataFrame(
            CSVReader& reader,
            const std::string& _key_column,
            DuplicateKeyPolicy policy = DuplicateKeyPolicy::OVERWRITE,
            bool throw_on_missing_key = true
        ) : DataFrame(
            reader,
            DataFrameOptions()
                .set_key_column(_key_column)
                .set_duplicate_key_policy(policy)
                .set_throw_on_missing_key(throw_on_missing_key)
        ) {}

        /** Construct a keyed DataFrame using a custom key function.
         *
         * @throws std::runtime_error if policy is THROW and duplicate keys are encountered
         */
        template<
            typename KeyFunc,
            csv::enable_if_t<csv::is_invocable_returning<KeyFunc, KeyType, const CSVRow&>::value, int> = 0
        >
        DataFrame(
            CSVReader& reader,
            KeyFunc key_func,
            DuplicateKeyPolicy policy = DuplicateKeyPolicy::OVERWRITE
        ) : col_names_(reader.col_names_ptr()) {
            this->is_keyed = true;
            this->key_column_index_ = CSV_NOT_FOUND;
            this->build_from_key_function(reader, key_func, policy);
        }

        /** Construct a keyed DataFrame using a custom key function with options. */
        template<
            typename KeyFunc,
            csv::enable_if_t<csv::is_invocable_returning<KeyFunc, KeyType, const CSVRow&>::value, int> = 0
        >
        DataFrame(
            CSVReader& reader,
            KeyFunc key_func,
            const DataFrameOptions& options
        ) : DataFrame(reader, key_func, options.get_duplicate_key_policy()) {}

        /** Get the number of rows in the DataFrame. */
        size_t size() const noexcept {
            return rows.size();
        }

        /** Check if the DataFrame is empty (has no rows). */
        bool empty() const noexcept {
            return rows.empty();
        }

        /** Get the number of rows in the DataFrame. Alias for size(). */
        size_t n_rows() const noexcept { return rows.size(); }
        
        /** Get the number of columns in the DataFrame. */
        size_t n_cols() const noexcept { return col_names_->size(); }

        /** Check if a column exists in the DataFrame. */
        bool has_column(const std::string& name) const {
            return this->index_of(name) != CSV_NOT_FOUND;
        }

        /** Get the index of a column by name. */
        int index_of(const std::string& name) const {
            return this->col_names_->index_of(name);
        }

        /** Get the column names in order. */
        const std::vector<std::string>& columns() const noexcept { return this->col_names_->get_col_names(); }

        /** Insert a new row at the specified index. Structural mutation invalidates outstanding row and cell proxies. */
        void insert_row(size_t index, const std::vector<std::string>& row) {
            if (index > this->rows.size()) {
                throw std::out_of_range("DataFrame insert_row index out of range");
            }

            this->validate_inserted_row(row);
            CSVRow inserted_row = this->make_inserted_csv_row(row, this->col_names_);

            if (this->is_keyed) {
                if (this->key_column_index_ == CSV_NOT_FOUND) {
                    throw std::runtime_error("insert_row requires a column-keyed DataFrame");
                }

                KeyType key = inserted_row[static_cast<size_t>(this->key_column_index_)].template get<KeyType>();
                this->ensure_key_index();
                if (this->key_index->find(key) != this->key_index->end()) {
                    throw std::runtime_error(internals::ERROR_DUPLICATE_KEY);
                }

                this->keys_.insert(this->keys_.begin() + index, std::move(key));
            }

            this->rows.insert(this->rows.begin() + index, std::move(inserted_row));
            this->edits.insert(this->edits.begin() + index, RowOverlaySlot());
            this->invalidate_key_index();
        }

        /** Insert a new column with empty values. Structural mutation materializes visible rows into fresh row storage. */
        void insert_column(size_t index, const std::string& name) {
            this->insert_column(index, name, std::string());
        }

        /** Insert a new column with a default value. Structural mutation materializes visible rows into fresh row storage. */
        void insert_column(size_t index, const std::string& name, const std::string& default_value) {
            if (index > this->n_cols()) {
                throw std::out_of_range("DataFrame insert_column index out of range");
            }

            this->validate_inserted_column_name(name);

            std::vector<std::string> new_columns = this->columns();
            new_columns.insert(new_columns.begin() + index, name);

            std::stringstream serialized;
            auto writer = make_csv_writer(serialized);
            writer << new_columns;

            const DataFrame& self = *this;
            for (size_t row_index = 0; row_index < this->rows.size(); ++row_index) {
                std::vector<std::string> row_values = self.at(row_index);
                row_values.insert(row_values.begin() + index, default_value);
                writer << row_values;
            }

            this->replace_with_reparsed_column_insert(
                serialized.str(),
                this->col_names_->get_policy(),
                index
            );
        }

        /** Append a new column with empty values. */
        void append_column(const std::string& name) {
            this->insert_column(this->n_cols(), name);
        }

        /** Append a new column with a default value. */
        void append_column(const std::string& name, const std::string& default_value) {
            this->insert_column(this->n_cols(), name, default_value);
        }

        /** Build an unkeyed DataFrame containing rows whose corresponding mask entry is true.
         *
         *  CSVRow copies share the underlying parsed row storage, so this is intended for
         *  filtered document views that should avoid reparsing or rematerializing fields.
         */
        DataFrame selected_rows(const std::vector<std::uint8_t>& include_rows) const {
            if (include_rows.size() != this->rows.size()) {
                throw std::invalid_argument("selected row mask size must match DataFrame row count");
            }

            std::vector<CSVRow> selected;
            selected.reserve(this->rows.size());
            for (size_t row_index = 0; row_index < this->rows.size(); ++row_index) {
                if (include_rows[row_index]) {
                    selected.push_back(this->rows[row_index]);
                }
            }

            return DataFrame(std::move(selected));
        }

        /** Access a column view by position. */
        DataFrameColumn<KeyType> column_view(size_t col_index) const {
            if (col_index >= this->n_cols()) {
                internals::throw_column_index_out_of_range();
            }

            return DataFrameColumn<KeyType>(this, col_index);
        }

        /** Access a column view by name. */
        DataFrameColumn<KeyType> column_view(const std::string& name) const {
            return this->column_view(this->find_column(name));
        }

        /**
         * Access a row by position (unchecked).
         *
         * @note Disabled when KeyType is an integral type to prevent ambiguity with
         *       operator[](const KeyType&). Use at(size_t) for positional access
         *       on integer-keyed DataFrames.
         *
         * @throws std::out_of_range if index is out of bounds (via std::vector::at)
         */
        template<typename K = KeyType,
            csv::enable_if_t<!std::is_integral<K>::value, int> = 0>
        DataFrameRow<KeyType> operator[](size_t i) {
            static_assert(std::is_same<K, KeyType>::value,
                "Do not explicitly instantiate this template. Use at(size_t) for positional access.");
            return this->at(i);
        }

        /** Access a row by position (unchecked, const version).
         *  Disabled when KeyType is an integral type — use at(size_t) instead. */
        template<typename K = KeyType,
            csv::enable_if_t<!std::is_integral<K>::value, int> = 0>
        DataFrameRow<KeyType> operator[](size_t i) const {
            static_assert(std::is_same<K, KeyType>::value,
                "Do not explicitly instantiate this template. Use at(size_t) for positional access.");
            return this->at(i);
        }

        /**
         * Access a row by position with bounds checking.
         *
         * @throws std::out_of_range if index is out of bounds
         */
        DataFrameRow<KeyType> at(size_t i) {
            const auto& row = rows.at(i);
            auto* row_edits = this->ensure_row_edits(i);
            return DataFrameRow<KeyType>(&row, this, i, row_edits, this->key_ptr_at(i));
        }
        
        /** Access a row by position with bounds checking (const version). */
        DataFrameRow<KeyType> at(size_t i) const {
            const auto& row = rows.at(i);
            const RowOverlay* row_edits = this->find_row_edits(i);
            return DataFrameRow<KeyType>(&row, this, i, row_edits, this->key_ptr_at(i));
        }

        /**
         * Access a row by its key.
         *
         * @throws std::runtime_error if the DataFrame was not created with a key column
         * @throws std::out_of_range if the key is not found
         */
        DataFrameRow<KeyType> operator[](const KeyType& key) {
            this->require_keyed_frame();
            auto position = this->position_of(key);
            return DataFrameRow<KeyType>(&rows.at(position), this, position, this->ensure_row_edits(position), this->key_ptr_at(position));
        }

        /** Access a row by its key (const version). */
        DataFrameRow<KeyType> operator[](const KeyType& key) const {
            this->require_keyed_frame();
            auto position = this->position_of(key);
            const RowOverlay* row_edits = this->find_row_edits(position);
            return DataFrameRow<KeyType>(&rows.at(position), this, position, row_edits, this->key_ptr_at(position));
        }

        /**
         * Check if a key exists in the DataFrame.
         *
         * @throws std::runtime_error if the DataFrame was not created with a key column
         */
        bool contains(const KeyType& key) const {
            this->require_keyed_frame();
            this->ensure_key_index();
            return key_index->find(key) != key_index->end();
        }

        /**
         * Extract all values from a column with type conversion.
         * Accounts for edited values in the overlay.
         *
         * @tparam T Target type for conversion (default: std::string)
         * @throws std::out_of_range if column is not found
         */
        template<typename T = std::string>
        std::vector<T> column(const std::string& name) const {
            const size_t col_idx = this->find_column(name);
            std::vector<T> values;

            values.reserve(rows.size());
            for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
                values.push_back(this->at(row_index)[col_idx].template get<T>());
            }

            return values;
        }

        /** Apply a batch-oriented function to each column, potentially in parallel.
         *
         * The callback receives a lightweight column view plus a mutable per-column
         * state object from `states`.
         *
         * Callbacks may safely perform read-only access through the provided
         * column view and any explicit read-only references they already hold to
         * this batch-scoped DataFrame. Sparse-overlay cell edits through
         * `DataFrameRow` or `DataFrameCell` are synchronized at row granularity,
         * but structural mutations such as `erase()` are not thread-safe.
         *
         * @throws std::invalid_argument if `states.size() != n_cols()`
         */
        template<typename State, typename Fn>
        void column_parallel_apply(
            DataFrameExecutor& executor,
            std::vector<State>& states,
            Fn&& fn
        ) const {
            if (states.size() != this->n_cols()) {
                throw std::invalid_argument(internals::ERROR_COLUMN_APPLY_STATE_COUNT);
            }

            executor.parallel_for(this->n_cols(), [this, &states, &fn](size_t column_index) {
                fn(this->column_view(column_index), states[column_index]);
            });
        }

        /** Apply a batch-oriented function to a selected subset of columns, potentially in parallel.
         *
         * The callback receives a lightweight column view plus a mutable
         * per-selected-column state object from `states`.
         *
         * @throws std::invalid_argument if `states.size() != column_indices.size()`
         * @throws std::out_of_range if any column index is invalid
         */
        template<typename State, typename Fn>
        void column_parallel_apply(
            DataFrameExecutor& executor,
            const std::vector<size_t>& column_indices,
            std::vector<State>& states,
            Fn&& fn
        ) const {
            if (states.size() != column_indices.size()) {
                throw std::invalid_argument(internals::ERROR_COLUMN_APPLY_SUBSET_STATE_COUNT);
            }

            this->validate_selected_columns(column_indices);

            executor.parallel_for(column_indices.size(), [this, &column_indices, &states, &fn](size_t selected_index) {
                const size_t column_index = column_indices[selected_index];
                fn(this->column_view(column_index), states[selected_index]);
            });
        }

        /** Apply a read-only batch function to each column, potentially in parallel.
         *
         * This overload is for callers who do not need one explicit mutable
         * state object per column and prefer to manage any output storage
         * externally.
         */
        template<typename Fn>
        void column_parallel_apply(
            DataFrameExecutor& executor,
            Fn&& fn
        ) const {
            executor.parallel_for(this->n_cols(), [this, &fn](size_t column_index) {
                fn(this->column_view(column_index));
            });
        }

        /** Apply a read-only batch function to a selected subset of columns, potentially in parallel.
         *
         * This overload is for callers who want to process only specific
         * columns and prefer to manage any output storage externally.
         *
         * @throws std::out_of_range if any column index is invalid
         */
        template<typename Fn>
        void column_parallel_apply(
            DataFrameExecutor& executor,
            const std::vector<size_t>& column_indices,
            Fn&& fn
        ) const {
            this->validate_selected_columns(column_indices);

            executor.parallel_for(column_indices.size(), [this, &column_indices, &fn](size_t selected_index) {
                fn(this->column_view(column_indices[selected_index]));
            });
        }

        /**
         * Group row positions using an arbitrary grouping function.
         *
         * @tparam GroupFunc Callable that takes a DataFrameRow and returns a hashable key
         */
        template<
            typename GroupFunc,
            typename GroupKey = invoke_result_t<GroupFunc, DataFrameRow<KeyType>>,
            csv::enable_if_t<
                internals::is_hashable<GroupKey>::value &&
                internals::is_equality_comparable<GroupKey>::value,
                int
            > = 0
        >
        std::unordered_map<GroupKey, std::vector<size_t>> group_by(GroupFunc group_func) const {
            std::unordered_map<GroupKey, std::vector<size_t>> grouped;

            for (size_t i = 0; i < rows.size(); i++) {
                GroupKey group_key = group_func(this->at(i));
                grouped[group_key].push_back(i);
            }

            return grouped;
        }

        /**
         * Group row positions by the value of a column.
         *
         * @throws std::out_of_range if column is not found
         */
        std::unordered_map<std::string, std::vector<size_t>> group_by(const std::string& name) const {
            const size_t col_idx = this->find_column(name);
            std::unordered_map<std::string, std::vector<size_t>> grouped;

            for (size_t i = 0; i < rows.size(); i++) {
                grouped[this->at(i)[col_idx].template get<std::string>()].push_back(i);
            }

            return grouped;
        }

        /** Get iterator to the first row. */
        iterator begin() { return iterator(this, 0); }
        
        /** Get iterator past the last row. */
        iterator end() { return iterator(this, this->size()); }

        /** Get const iterator to the first row. */
        const_iterator begin() const { return const_iterator(this, 0); }

        /** Get const iterator past the last row. */
        const_iterator end() const { return const_iterator(this, this->size()); }

        /** Get const iterator to the first row (explicit). */
        const_iterator cbegin() const { return const_iterator(this, 0); }

        /** Get const iterator past the last row (explicit). */
        const_iterator cend() const { return const_iterator(this, this->size()); }

    private:
        /** Whether this DataFrame was created with a key. */
        bool is_keyed = false;
        
        /** Column names in order. */
        internals::ConstColNamesPtr col_names_ = std::make_shared<internals::ColNames>();
        
        /** Internal storage for row data. */
        std::vector<CSVRow> rows;

        /** Stored keys for keyed DataFrames only. Empty for unkeyed frames. */
        std::vector<KeyType> keys_;

        /** Column index used for keyed frames constructed from a named column. */
        int key_column_index_ = CSV_NOT_FOUND;

        /** Lazily-built index mapping keys to row positions (mutable for const methods). */
        mutable std::unique_ptr<std::unordered_map<KeyType, size_t>> key_index;
        mutable internals::lazy_shared_ptr<internals::JsonConverter> json_converter_;

        /**
         * Sparse per-row edit overlays. Slots stay cheap at load time; the
         * heavier synchronized overlay map is allocated only for rows that are
         * actually edited.
         */
        std::vector<RowOverlaySlot> edits;
        std::shared_ptr<std::mutex> edits_creation_lock_{ new std::mutex() };

        /** Initialize an unkeyed DataFrame from a CSV reader. */
        void init_unkeyed_from_reader(CSVReader& reader) {
            this->assert_fresh_storage(false);
            this->is_keyed = false;
            this->col_names_ = reader.col_names_ptr();

            std::vector<CSVRow> batch;
            while (reader.read_chunk(batch, dataframe_read_chunk_rows())) {
                this->append_unkeyed_batch(batch);
            }
        }

        /** Initialize an unkeyed DataFrame from an existing row batch. */
        void init_unkeyed_from_rows(std::vector<CSVRow>& source_rows) {
            this->assert_fresh_storage(false);
            this->is_keyed = false;
            this->col_names_ = source_rows.empty()
                ? internals::ConstColNamesPtr(std::make_shared<internals::ColNames>())
                : source_rows.front().col_names_ptr();
            this->rows = std::move(source_rows);
            this->edits.resize(this->rows.size());
        }

        /** Initialize a keyed DataFrame from a CSV reader using column-based key extraction. */
        void init_from_reader(CSVReader& reader, const DataFrameOptions& options) {
            this->assert_fresh_storage(false);
            this->is_keyed = true;
            this->col_names_ = reader.col_names_ptr();
            const std::string key_column = options.get_key_column();

            if (key_column.empty())
                throw std::runtime_error(internals::ERROR_KEY_COLUMN_EMPTY);

            const int key_column_index = this->col_names_->index_of(key_column);
            if (key_column_index == CSV_NOT_FOUND)
                throw std::runtime_error(std::string(internals::ERROR_KEY_COLUMN_NOT_FOUND) + key_column);
            this->key_column_index_ = key_column_index;

            const bool throw_on_missing_key = options.get_throw_on_missing_key();

            this->build_from_key_function(
                reader,
                [key_column, throw_on_missing_key](const CSVRow& row) -> KeyType {
                    try {
                        return row[key_column].template get<KeyType>();
                    }
                    catch (const std::exception& e) {
                        if (throw_on_missing_key) {
                            throw std::runtime_error(internals::ERROR_KEY_COLUMN_VALUE + std::string(e.what()));
                        }

                        return KeyType();
                    }
                },
                options.get_duplicate_key_policy()
            );
        }

        /** Build keyed DataFrame using a custom key extraction function. */
        template<typename KeyFunc>
        void build_from_key_function(
            CSVReader& reader,
            KeyFunc key_func,
            DuplicateKeyPolicy policy
        ) {
            std::unordered_map<KeyType, size_t> key_to_pos;
            this->assert_fresh_storage(true);

            std::vector<CSVRow> batch;
            while (reader.read_chunk(batch, dataframe_read_chunk_rows())) {
                this->append_keyed_batch(batch, key_func, policy, key_to_pos);
            }
        }

        static size_t dataframe_read_chunk_rows() noexcept {
            return 50000;
        }

        template<typename Container>
        static void reserve_for_append(Container& container, size_t additional) {
            if (additional == 0) {
                return;
            }

            const size_t required = container.size() + additional;
            if (required <= container.capacity()) {
                return;
            }

            const size_t current = container.capacity();
            // Do not reserve exactly one batch ahead. DataFrame construction
            // appends fixed-size read_chunk() batches, so exact reserves would
            // force a reallocation on every batch for large inputs.
            size_t next = current == 0 ? additional : current * 2;
            if (next < required || next < current) {
                next = required;
            }

            container.reserve(next);
        }

        void append_unkeyed_batch(std::vector<CSVRow>& batch) {
            reserve_for_append(rows, batch.size());
            reserve_for_append(edits, batch.size());

            for (auto& row : batch) {
                rows.push_back(std::move(row));
                edits.emplace_back();
            }
        }

        template<typename KeyFunc>
        void append_keyed_batch(
            std::vector<CSVRow>& batch,
            KeyFunc& key_func,
            DuplicateKeyPolicy policy,
            std::unordered_map<KeyType, size_t>& key_to_pos
        ) {
            reserve_for_append(rows, batch.size());
            reserve_for_append(keys_, batch.size());
            reserve_for_append(edits, batch.size());

            for (auto& row : batch) {
                KeyType key = key_func(row);

                auto existing = key_to_pos.find(key);
                if (existing != key_to_pos.end()) {
                    if (policy == DuplicateKeyPolicy::THROW)
                        throw std::runtime_error(internals::ERROR_DUPLICATE_KEY);

                    if (policy == DuplicateKeyPolicy::OVERWRITE)
                        rows[existing->second] = std::move(row);

                    continue;
                }

                rows.push_back(std::move(row));
                keys_.push_back(key);
                edits.emplace_back();
                key_to_pos[key] = rows.size() - 1;
            }
        }

        /** Find the index of a column by name. Throws if the column is not found. */
        size_t find_column(const std::string& name) const {
            return index_of(name) != CSV_NOT_FOUND ? static_cast<size_t>(index_of(name)) :
                throw std::out_of_range(std::string(internals::ERROR_COLUMN_NOT_FOUND) + name);
        }

        /** Return the overlay for a specific row index. */
        const RowOverlay* find_row_edits(size_t row_index) const {
            return edits.at(row_index).get();
        }

        /** Return the row overlay, allocating it only when mutable access needs it. */
        RowOverlay* ensure_row_edits(size_t row_index) {
            RowOverlaySlot& slot = edits.at(row_index);
            RowOverlay* overlay = slot.get();
            if (overlay) {
                return overlay;
            }

            std::lock_guard<std::mutex> lock(*edits_creation_lock_);
            return slot.ensure();
        }

        DataFrameRow<KeyType> make_row_proxy(size_t row_index) {
            const auto& row = rows.at(row_index);
            return DataFrameRow<KeyType>(&row, this, row_index, this->ensure_row_edits(row_index), this->key_ptr_at(row_index));
        }

        DataFrameRow<KeyType> make_const_row_proxy(size_t row_index) const {
            const auto& row = rows.at(row_index);
            return DataFrameRow<KeyType>(&row, this, row_index, this->find_row_edits(row_index), this->key_ptr_at(row_index));
        }

        void erase_row_edits(size_t row_index) {
            if (row_index < edits.size()) {
                edits.erase(edits.begin() + row_index);
            }
        }

        static CSVRow make_inserted_csv_row(
            const std::vector<std::string>& row,
            internals::ConstColNamesPtr col_names
        ) {
            internals::RawCSVDataPtr data = std::make_shared<internals::RawCSVData>();
            data->col_names = std::const_pointer_cast<internals::ColNames>(col_names);

            size_t total_bytes = 0;
            for (const auto& field : row) {
                total_bytes += field.size();
            }
            data->quote_arena.reserve_for_source_size(total_bytes);
            data->fields.reserve_for_source_size(row.size());

            for (const auto& field : row) {
                const size_t offset = data->quote_arena.append(csv::string_view(field));
                data->fields.emplace_back(offset, field.size(), true);
            }

            return CSVRow(data, 0, 0, row.size());
        }

        void validate_inserted_row(const std::vector<std::string>& row) const {
            if (this->col_names_ && !this->col_names_->empty() && row.size() != this->n_cols()) {
                throw std::invalid_argument("inserted row field count must match DataFrame column count");
            }
        }

        void validate_inserted_column_name(const std::string& name) const {
            if (name.empty()) {
                throw std::invalid_argument("inserted column name must not be empty");
            }

            if (this->has_column(name)) {
                throw std::invalid_argument("inserted column name must not duplicate an existing column");
            }
        }

        std::vector<KeyType> build_keys_from_rows(
            const std::vector<CSVRow>& source_rows,
            size_t key_column_index
        ) const {
            std::vector<KeyType> rebuilt_keys;
            std::unordered_map<KeyType, size_t> seen_keys;
            rebuilt_keys.reserve(source_rows.size());

            for (size_t row_index = 0; row_index < source_rows.size(); ++row_index) {
                KeyType key = source_rows[row_index][key_column_index].template get<KeyType>();
                auto inserted = seen_keys.emplace(key, row_index);
                if (!inserted.second) {
                    throw std::runtime_error(internals::ERROR_DUPLICATE_KEY);
                }

                rebuilt_keys.push_back(std::move(key));
            }

            return rebuilt_keys;
        }

        void replace_with_reparsed_column_insert(
            const std::string& serialized,
            ColumnNamePolicy column_name_policy,
            size_t inserted_column_index
        ) {
            const bool was_keyed = this->is_keyed;
            const int old_key_column_index = this->key_column_index_;
            const int new_key_column_index =
                old_key_column_index != CSV_NOT_FOUND &&
                inserted_column_index <= static_cast<size_t>(old_key_column_index)
                    ? old_key_column_index + 1
                    : old_key_column_index;

            CSVFormat format;
            format.delimiter(',')
                .header_row(0)
                .column_names_policy(column_name_policy);

            std::istringstream input(serialized);
            CSVReader reader(input, format);
            DataFrame<KeyType> rebuilt(reader);

            std::vector<KeyType> rebuilt_keys;
            if (was_keyed && new_key_column_index != CSV_NOT_FOUND) {
                rebuilt_keys = this->build_keys_from_rows(
                    rebuilt.rows,
                    static_cast<size_t>(new_key_column_index)
                );
            }

            this->col_names_ = rebuilt.col_names_;
            this->rows = std::move(rebuilt.rows);
            this->edits.clear();
            this->edits.resize(this->rows.size());

            if (was_keyed) {
                this->is_keyed = true;
                this->key_column_index_ = new_key_column_index;
                if (new_key_column_index != CSV_NOT_FOUND) {
                    this->keys_ = std::move(rebuilt_keys);
                }
            }
            else {
                this->is_keyed = false;
                this->key_column_index_ = CSV_NOT_FOUND;
                this->keys_.clear();
            }

            this->invalidate_key_index();
            this->invalidate_json_converter();
        }

        bool erase_at_index(size_t row_index) {
            if (row_index >= rows.size()) {
                return false;
            }

            this->erase_row_edits(row_index);
            rows.erase(rows.begin() + row_index);
            if (this->is_keyed) {
                keys_.erase(keys_.begin() + row_index);
            }
            this->invalidate_key_index();
            return true;
        }

        void validate_selected_columns(const std::vector<size_t>& column_indices) const {
            for (size_t column_index : column_indices) {
                if (column_index >= this->n_cols()) {
                    throw std::out_of_range(internals::ERROR_COLUMN_APPLY_INVALID_INDEX);
                }
            }
        }

        /** Validate that this DataFrame was created with a key column. */
        void require_keyed_frame() const {
            if (!is_keyed)
                throw std::runtime_error(internals::ERROR_UNKEYED_DATA_FRAME);
        }

        /** Invalidate the lazy key index after structural changes. */
        void invalidate_key_index() {
            key_index.reset();
        }

        /** Invalidate cached JSON column mapping after structural column changes. */
        void invalidate_json_converter() {
            json_converter_ = internals::lazy_shared_ptr<internals::JsonConverter>();
        }

        /** Debug-only check that constructor helpers are starting from a pristine batch state. */
        void assert_fresh_storage(bool expected_is_keyed) const {
            CSV_DEBUG_ASSERT(this->rows.empty());
            CSV_DEBUG_ASSERT(this->keys_.empty());
            CSV_DEBUG_ASSERT(this->edits.empty());
            CSV_DEBUG_ASSERT(this->key_index.get() == nullptr);
            CSV_DEBUG_ASSERT(this->json_converter_.get() == nullptr);
            CSV_DEBUG_ASSERT(this->is_keyed == expected_is_keyed);
        }

        /** Build the key index if it doesn't exist (lazy initialization). */
        void ensure_key_index() const {
            if (key_index) return;

            key_index = std::unique_ptr<std::unordered_map<KeyType, size_t>>(
                new std::unordered_map<KeyType, size_t>()
            );

            for (size_t i = 0; i < rows.size(); i++) {
                (*key_index)[keys_[i]] = i;
            }
        }

        /** Find the position of a key in the rows vector. */
        size_t position_of(const KeyType& key) const {
            this->ensure_key_index();
            auto it = key_index->find(key);
            return it == key_index->end() ? throw std::out_of_range(internals::ERROR_KEY_NOT_FOUND)
                : it->second;
        }

        const KeyType* key_ptr_at(size_t row_index) const {
            return this->is_keyed ? &keys_.at(row_index) : nullptr;
        }
    };

#ifdef CSV_HAS_CXX20
    static_assert(
        internals::csv_write_rows_input_range<DataFrame<>>,
        "DataFrame must remain compatible with csv::DelimWriter::write_rows()."
    );
#endif
}
