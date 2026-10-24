#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
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
#include "data_frame_storage.hpp"
#include "fwd.hpp"
#include "key_storage.hpp"
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
#if CSV_EXCEPTIONS_ENABLED
        explicit DataFrame(CSVReader& reader) {
            CSVError error;
            this->init_unkeyed_from_reader(reader, error);
            internals::throw_dataframe_error(error);
        }
#endif

        /** Construct an unkeyed DataFrame directly from a CSV file.
         *  By default, the delimiter and header are detected as for CSVReader.
         *  Pass a CSVFormat to configure parsing explicitly.
         */
#if CSV_EXCEPTIONS_ENABLED
        explicit DataFrame(
            csv::string_view filename,
            CSVFormat format = CSVFormat::guess_csv()
        ) {
            CSVReader reader(filename, format);
            CSVError error;
            this->init_unkeyed_from_reader(reader, error);
            internals::throw_dataframe_error(error);
        }
#endif

        /** Construct an unkeyed DataFrame from an existing batch of rows. */
        explicit DataFrame(std::vector<CSVRow> rows) {
            this->init_unkeyed_from_rows(rows);
        }

        /** Construct a keyed DataFrame from a CSV reader with options.
         *
         * @throws std::runtime_error if key column is empty or not found
         */
#if CSV_EXCEPTIONS_ENABLED
        explicit DataFrame(CSVReader& reader, const DataFrameOptions& options) {
            CSVError error;
            this->init_from_reader(reader, options, error);
            internals::throw_dataframe_error(error);
        }
#endif

        /** Construct a keyed DataFrame directly from a CSV file.
         *
         * @throws std::runtime_error if key column is empty or not found
         */
#if CSV_EXCEPTIONS_ENABLED
        DataFrame(
            csv::string_view filename,
            const DataFrameOptions& options,
            CSVFormat format = CSVFormat::guess_csv()
        ) {
            CSVReader reader(filename, format);
            CSVError error;
            this->init_from_reader(reader, options, error);
            internals::throw_dataframe_error(error);
        }
#endif

        /** Construct a keyed DataFrame using a column name as the key.
         *
         * @throws std::runtime_error if key column is empty or not found
         */
#if CSV_EXCEPTIONS_ENABLED
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
#endif

        /** Construct a keyed DataFrame using a custom key function.
         *
         * @throws std::runtime_error if policy is THROW and duplicate keys are encountered
         */
#if CSV_EXCEPTIONS_ENABLED
        template<
            typename KeyFunc,
            csv::enable_if_t<csv::is_invocable_returning<KeyFunc, KeyType, const CSVRow&>::value, int> = 0
        >
        DataFrame(
            CSVReader& reader,
            KeyFunc key_func,
            DuplicateKeyPolicy policy = DuplicateKeyPolicy::OVERWRITE
        ) {
            CSVError error;
            this->init_custom_from_reader(reader, key_func, policy, error);
            internals::throw_dataframe_error(error);
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

#endif

        /** Read a frame, returning no object on validation or input failure. */
        static std::unique_ptr<DataFrame> try_create(CSVReader& reader, CSVError& error) {
            std::unique_ptr<DataFrame> result(new DataFrame());
            if (!result->init_unkeyed_from_reader(reader, error)) return nullptr;
            return result;
        }

        static std::unique_ptr<DataFrame> try_create(
            CSVReader& reader, const DataFrameOptions& options, CSVError& error
        ) {
            std::unique_ptr<DataFrame> result(new DataFrame());
            if (!result->init_from_reader(reader, options, error)) return nullptr;
            return result;
        }

        static std::unique_ptr<DataFrame> try_create(
            csv::string_view filename, CSVError& error,
            const CSVFormat& format = CSVFormat::guess_csv()
        ) {
            auto reader = CSVReader::try_open(filename, error, format);
            return reader ? try_create(*reader, error) : nullptr;
        }

        static std::unique_ptr<DataFrame> try_create(
            csv::string_view filename, const DataFrameOptions& options, CSVError& error,
            const CSVFormat& format = CSVFormat::guess_csv()
        ) {
            auto reader = CSVReader::try_open(filename, error, format);
            return reader ? try_create(*reader, options, error) : nullptr;
        }

        template<typename KeyFunc,
            csv::enable_if_t<csv::is_invocable_returning<KeyFunc, KeyType, const CSVRow&>::value, int> = 0>
        static std::unique_ptr<DataFrame> try_create(
            CSVReader& reader, KeyFunc key_func, CSVError& error,
            DuplicateKeyPolicy policy = DuplicateKeyPolicy::OVERWRITE
        ) {
            std::unique_ptr<DataFrame> result(new DataFrame());
            if (!result->init_custom_from_reader(reader, key_func, policy, error)) return nullptr;
            return result;
        }

        template<typename KeyFunc,
            csv::enable_if_t<csv::is_invocable_returning<KeyFunc, KeyType, const CSVRow&>::value, int> = 0>
        static std::unique_ptr<DataFrame> try_create(
            CSVReader& reader, KeyFunc key_func, const DataFrameOptions& options, CSVError& error
        ) {
            return try_create(reader, std::move(key_func), error, options.get_duplicate_key_policy());
        }

        /** Get the number of rows in the DataFrame. */
        size_t size() const noexcept {
            return storage_.rows().size();
        }

        /** Check if the DataFrame is empty (has no rows). */
        bool empty() const noexcept {
            return storage_.rows().empty();
        }

        /** Get the number of rows in the DataFrame. Alias for size(). */
        size_t n_rows() const noexcept { return storage_.rows().size(); }
        
        /** Get the number of visible columns in the DataFrame. */
        size_t n_cols() const noexcept { return storage_.n_cols(); }

        /** Check if a column exists in the DataFrame. */
        bool has_column(const std::string& name) const {
            return this->index_of(name) != CSV_NOT_FOUND;
        }

        /** Get the index of a column by name. */
        int index_of(const std::string& name) const {
            return this->storage_.names()->index_of(name);
        }

        /** Get the column names in order. */
        const std::vector<std::string>& columns() const noexcept { return this->storage_.names()->get_col_names(); }

        /** Insert a new row at the specified index. Structural mutation invalidates outstanding row and cell proxies. */
#if CSV_EXCEPTIONS_ENABLED
        void insert_row(size_t index, const std::vector<std::string>& row) {
            CSVError error;
            this->try_insert_row(index, row, error);
            internals::throw_dataframe_error(error);
        }
#endif

        /** Insert a new column with empty values.
         *  Materializes visible rows into fresh row storage and invalidates outstanding proxies.
         */
#if CSV_EXCEPTIONS_ENABLED
        void insert_column(size_t index, const std::string& name) {
            this->insert_column(index, name, std::string());
        }
#endif

        /** Insert a new column with a default value.
         *  Materializes visible rows into fresh row storage and invalidates outstanding proxies.
         *  Established row keys remain unchanged even when key-column values have been edited.
         */
#if CSV_EXCEPTIONS_ENABLED
        void insert_column(size_t index, const std::string& name, const std::string& default_value) {
            CSVError error;
            this->try_insert_column(index, name, default_value, error);
            internals::throw_dataframe_error(error);
        }
#endif

        /** Append a new column with empty values. */
#if CSV_EXCEPTIONS_ENABLED
        void append_column(const std::string& name) {
            this->insert_column(this->n_cols(), name);
        }
#endif

        /** Append a new column with a default value. */
#if CSV_EXCEPTIONS_ENABLED
        void append_column(const std::string& name, const std::string& default_value) {
            this->insert_column(this->n_cols(), name, default_value);
        }
#endif

        /** Build an unkeyed DataFrame containing rows whose corresponding mask entry is true.
         *
         *  CSVRow copies share the underlying parsed row storage, so this is intended for
         *  filtered document views that should avoid reparsing or rematerializing fields.
         *  Existing cell edits are copied; subsequent edits to either frame are independent.
         */
#if CSV_EXCEPTIONS_ENABLED
        DataFrame selected_rows(const std::vector<std::uint8_t>& include_rows) const {
            CSVError error;
            DataFrame result = this->try_selected_rows(include_rows, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Insert a row; validation failures leave the frame unchanged. */
        bool try_insert_row(size_t index, const std::vector<std::string>& row, CSVError& error) {
            error.clear();
            if (index > this->size())
                return internals::fail(error, CSVErrorCode::IndexOutOfBounds, "DataFrame insert_row index out of range");
            CSVRow inserted_row = this->storage_.make_inserted_row(row, error);
            if (error) return false;
            if (this->is_keyed_) {
                if (this->key_column_index_ == CSV_NOT_FOUND)
                    return internals::fail(error, CSVErrorCode::UnsupportedOperation, "insert_row requires a column-keyed DataFrame");
                CSVField field = inserted_row.try_field(static_cast<size_t>(this->key_column_index_), error);
                if (error) return false;
                KeyType key{};
                CSVConversionError conversion;
                if (!field.try_get(key, conversion)) {
                    internals::fail(error, CSVErrorCode::ConversionFailed, csv_conversion_error_message(conversion));
                    error.conversion = conversion;
                    return false;
                }
                this->ensure_key_index();
                if (this->key_index_->find(key) != this->key_index_->end())
                    return internals::fail(error, CSVErrorCode::DuplicateKey, internals::ERROR_DUPLICATE_KEY);
                this->storage_.reserve_insert();
                reserve_for_append(this->keys_, 1);
                this->keys_.insert(this->keys_.begin() + index, std::move(key));
            } else { this->storage_.reserve_insert(); }
            this->storage_.insert_row(index, std::move(inserted_row));
            this->invalidate_key_index();
            return true;
        }

        /** Materialize the visible schema after validating the requested insertion. */
        bool try_insert_column(size_t index, const std::string& name, const std::string& value, CSVError& error) {
            if (!this->storage_.insert_column(index, name, value, this->key_column_index_, [this](CSVError& failure) {
                return !this->is_keyed_ || this->key_storage_.preserve(
                    this->keys_, [this]() { this->invalidate_key_index(); }, failure);
            }, error)) return false;
            this->invalidate_column_proxies();
            this->invalidate_json_converter();
            return true;
        }
        bool try_insert_column(size_t index, const std::string& name, CSVError& error) {
            return this->try_insert_column(index, name, std::string(), error);
        }
        bool try_append_column(const std::string& name, CSVError& error) {
            return this->try_insert_column(this->n_cols(), name, error);
        }
        bool try_append_column(const std::string& name, const std::string& value, CSVError& error) {
            return this->try_insert_column(this->n_cols(), name, value, error);
        }

        /** Selection preserves visible edits and returns an empty frame on failure. */
        DataFrame try_selected_rows(const std::vector<std::uint8_t>& mask, CSVError& error) const {
            DataFrame result;
            result.storage_ = this->storage_.selected_rows(mask, error);
            return result;
        }

        /** Access a column view by position. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameColumn<KeyType> column_view(size_t col_index) {
            CSVError error;
            auto result = this->try_column_view(col_index, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Access a column view by position. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameColumn<KeyType> column_view(size_t col_index) const {
            CSVError error;
            auto result = this->try_column_view(col_index, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Access a column view by name. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameColumn<KeyType> column_view(const std::string& name) {
            CSVError error;
            auto result = this->try_column_view(name, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Access a column view by name. */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameColumn<KeyType> column_view(const std::string& name) const {
            CSVError error;
            auto result = this->try_column_view(name, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /**
         * Access a row by position with bounds checking.
         *
         * @note Disabled when KeyType is an integral type to prevent ambiguity with
         *       operator[](const KeyType&). Use at(size_t) for positional access
         *       on integer-keyed DataFrames.
         *
         * @throws std::out_of_range if index is out of bounds (via std::vector::at)
         */
#if CSV_EXCEPTIONS_ENABLED
        template<typename K = KeyType,
            csv::enable_if_t<!std::is_integral<K>::value, int> = 0>
        DataFrameRow<KeyType> operator[](size_t i) {
            static_assert(std::is_same<K, KeyType>::value,
                "Do not explicitly instantiate this template. Use at(size_t) for positional access.");
            return this->at(i);
        }

        /** Access a row by position with bounds checking (const version).
         *  Disabled when KeyType is an integral type — use at(size_t) instead. */
        template<typename K = KeyType,
            csv::enable_if_t<!std::is_integral<K>::value, int> = 0>
        DataFrameRow<KeyType> operator[](size_t i) const {
            static_assert(std::is_same<K, KeyType>::value,
                "Do not explicitly instantiate this template. Use at(size_t) for positional access.");
            return this->at(i);
        }

#endif

        /**
         * Access a row by position with bounds checking.
         *
         * @throws std::out_of_range if index is out of bounds
         */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameRow<KeyType> at(size_t i) {
            CSVError error;
            auto result = this->try_at(i, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif
        
        /** Access a row by position with bounds checking (const version). */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameRow<KeyType> at(size_t i) const {
            CSVError error;
            auto result = this->try_at(i, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /**
         * Access a row by its key.
         * Stored keys identify rows independently of subsequent key-column cell edits.
         *
         * @throws std::runtime_error if the DataFrame was not created with a key column
         * @throws std::out_of_range if the key is not found
         */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameRow<KeyType> operator[](const KeyType& key) {
            CSVError error;
            auto result = this->try_find(key, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Access a row by its key (const version). */
#if CSV_EXCEPTIONS_ENABLED
        DataFrameRow<KeyType> operator[](const KeyType& key) const {
            CSVError error;
            auto result = this->try_find(key, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /**
         * Check if a key exists in the DataFrame.
         *
         * @throws std::runtime_error if the DataFrame was not created with a key column
         */
#if CSV_EXCEPTIONS_ENABLED
        bool contains(const KeyType& key) const {
            CSVError error;
            const bool result = this->try_contains(key, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /**
         * Extract all values from a column with type conversion.
         * Accounts for edited values in the overlay.
         *
         * @tparam T Target type for conversion (default: std::string)
         * @throws std::out_of_range if column is not found
         */
#if CSV_EXCEPTIONS_ENABLED
        template<typename T = std::string>
        std::vector<T> column(const std::string& name) const {
            CSVError error;
            auto result = this->template try_column<T>(name, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Checked positional row access. Failed lookups return an unbound proxy. */
        row_type try_at(size_t index, CSVError& error) {
            if (!this->validate_row_index(index, error)) return {};
            return this->make_row_proxy(index);
        }
        row_type try_at(size_t index, CSVError& error) const {
            if (!this->validate_row_index(index, error)) return {};
            return this->make_const_row_proxy(index);
        }
        row_type try_find(const KeyType& key, CSVError& error) {
            const size_t index = this->try_position_of(key, error);
            return error ? row_type() : this->make_row_proxy(index);
        }
        row_type try_find(const KeyType& key, CSVError& error) const {
            const size_t index = this->try_position_of(key, error);
            return error ? row_type() : this->make_const_row_proxy(index);
        }
        bool try_contains(const KeyType& key, CSVError& error) const {
            error.clear();
            if (!this->require_keyed_frame(error)) return false;
            this->ensure_key_index();
            return this->key_index_->find(key) != this->key_index_->end();
        }
        column_type try_column_view(size_t index, CSVError& error) {
            if (!this->validate_column_index(index, error)) return {};
            return column_type(this, index);
        }
        column_type try_column_view(size_t index, CSVError& error) const {
            if (!this->validate_column_index(index, error)) return {};
            return column_type(this, index);
        }
        column_type try_column_view(const std::string& name, CSVError& error) {
            const size_t index = this->try_find_column(name, error);
            return error ? column_type() : column_type(this, index);
        }
        column_type try_column_view(const std::string& name, CSVError& error) const {
            const size_t index = this->try_find_column(name, error);
            return error ? column_type() : column_type(this, index);
        }
        template<typename T = std::string>
        std::vector<T> try_column(const std::string& name, CSVError& error) const {
            const auto view = this->try_column_view(name, error);
            return error ? std::vector<T>() : view.template try_to_vector<T>(error);
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
#if CSV_EXCEPTIONS_ENABLED
        template<typename State, typename Fn>
        void column_parallel_apply(
            DataFrameExecutor& executor,
            std::vector<State>& states,
            Fn&& fn
        ) const {
            CSVError error;
            this->try_column_parallel_apply(executor, states, std::forward<Fn>(fn), error);
            internals::throw_dataframe_error(error);
        }
#endif

        template<typename State, typename Fn>
        bool try_column_parallel_apply(
            DataFrameExecutor& executor,
            std::vector<State>& states,
            Fn&& fn,
            CSVError& error
        ) const {
            error.clear();
            if (states.size() != this->n_cols()) {
                return internals::fail(error, CSVErrorCode::InvalidArgument, internals::ERROR_COLUMN_APPLY_STATE_COUNT);
            }

            executor.parallel_for(this->n_cols(), [this, &states, &fn](size_t column_index) {
                fn(column_type(this, column_index), states[column_index]);
            });
            return true;
        }

        /** Apply a batch-oriented function to a selected subset of columns, potentially in parallel.
         *
         * The callback receives a lightweight column view plus a mutable
         * per-selected-column state object from `states`.
         *
         * @throws std::invalid_argument if `states.size() != column_indices.size()`
         * @throws std::out_of_range if any column index is invalid
         */
#if CSV_EXCEPTIONS_ENABLED
        template<typename State, typename Fn>
        void column_parallel_apply(
            DataFrameExecutor& executor,
            const std::vector<size_t>& column_indices,
            std::vector<State>& states,
            Fn&& fn
        ) const {
            CSVError error;
            this->try_column_parallel_apply(executor, column_indices, states, std::forward<Fn>(fn), error);
            internals::throw_dataframe_error(error);
        }
#endif

        template<typename State, typename Fn>
        bool try_column_parallel_apply(
            DataFrameExecutor& executor,
            const std::vector<size_t>& column_indices,
            std::vector<State>& states,
            Fn&& fn,
            CSVError& error
        ) const {
            error.clear();
            if (states.size() != column_indices.size()) {
                return internals::fail(error, CSVErrorCode::InvalidArgument, internals::ERROR_COLUMN_APPLY_SUBSET_STATE_COUNT);
            }

            if (!this->validate_selected_columns(column_indices, error)) return false;

            executor.parallel_for(column_indices.size(), [this, &column_indices, &states, &fn](size_t selected_index) {
                const size_t column_index = column_indices[selected_index];
                fn(column_type(this, column_index), states[selected_index]);
            });
            return true;
        }

        /** Apply a read-only batch function to each column, potentially in parallel.
         *
         * This overload is for callers who do not need one explicit mutable
         * state object per column and prefer to manage any output storage
         * externally.
         */
#if CSV_EXCEPTIONS_ENABLED
        template<typename Fn>
        void column_parallel_apply(
            DataFrameExecutor& executor,
            Fn&& fn
        ) const {
            CSVError error;
            this->try_column_parallel_apply(executor, std::forward<Fn>(fn), error);
            internals::throw_dataframe_error(error);
        }
#endif

        template<typename Fn>
        bool try_column_parallel_apply(
            DataFrameExecutor& executor,
            Fn&& fn,
            CSVError& error
        ) const {
            error.clear();
            executor.parallel_for(this->n_cols(), [this, &fn](size_t column_index) {
                fn(column_type(this, column_index));
            });
            return true;
        }

        /** Apply a read-only batch function to a selected subset of columns, potentially in parallel.
         *
         * This overload is for callers who want to process only specific
         * columns and prefer to manage any output storage externally.
         *
         * @throws std::out_of_range if any column index is invalid
         */
#if CSV_EXCEPTIONS_ENABLED
        template<typename Fn>
        void column_parallel_apply(
            DataFrameExecutor& executor,
            const std::vector<size_t>& column_indices,
            Fn&& fn
        ) const {
            CSVError error;
            this->try_column_parallel_apply(executor, column_indices, std::forward<Fn>(fn), error);
            internals::throw_dataframe_error(error);
        }
#endif

        template<typename Fn>
        bool try_column_parallel_apply(
            DataFrameExecutor& executor,
            const std::vector<size_t>& column_indices,
            Fn&& fn,
            CSVError& error
        ) const {
            error.clear();
            if (!this->validate_selected_columns(column_indices, error)) return false;

            executor.parallel_for(column_indices.size(), [this, &column_indices, &fn](size_t selected_index) {
                fn(column_type(this, column_indices[selected_index]));
            });
            return true;
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

            for (size_t i = 0; i < storage_.rows().size(); i++) {
                GroupKey group_key = group_func(this->make_const_row_proxy(i));
                grouped[group_key].push_back(i);
            }

            return grouped;
        }

        /**
         * Group row positions by the value of a column.
         *
         * @throws std::out_of_range if column is not found
         */
#if CSV_EXCEPTIONS_ENABLED
        std::unordered_map<std::string, std::vector<size_t>> group_by(const std::string& name) const {
            CSVError error;
            auto result = this->try_group_by(name, error);
            internals::throw_dataframe_error(error);
            return result;
        }
#endif

        /** Group visible string values, reporting lookup or short-row errors. */
        std::unordered_map<std::string, std::vector<size_t>> try_group_by(const std::string& name, CSVError& error) const {
            std::unordered_map<std::string, std::vector<size_t>> grouped;
            const size_t column = this->try_find_column(name, error);
            if (error) return grouped;
            for (size_t i = 0; i < this->size(); ++i) {
                const auto cell = this->try_make_cell(i, column, false, error);
                if (error) return {};
                grouped[std::string(cell.get_sv())].push_back(i);
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
        bool is_keyed_ = false;
        
        internals::data_frame::DataFrameStorage storage_;

        /** Stored keys for keyed DataFrames only. Empty for unkeyed frames. */
        std::vector<KeyType> keys_;
        internals::data_frame::KeyStorage<KeyType> key_storage_;

        /** Column index used for keyed frames constructed from a named column. */
        int key_column_index_ = CSV_NOT_FOUND;

        /** Lazily-built index mapping keys to row positions (mutable for const methods). */
        mutable std::unique_ptr<std::unordered_map<KeyType, size_t>> key_index_;
        mutable internals::lazy_shared_ptr<internals::JsonConverter> json_converter_;
        std::shared_ptr<size_t> column_generation_{ new size_t(0) };

        /** Initialize an unkeyed DataFrame from a CSV reader. */
        bool init_unkeyed_from_reader(CSVReader& reader, CSVError& error) {
            error.clear();
            this->assert_fresh_storage(false);
            this->storage_.set_names(reader.col_names_ptr());
            std::vector<CSVRow> batch;
            while (reader.try_read_chunk(batch, dataframe_read_chunk_rows(), error)) {
                this->append_unkeyed_batch(batch);
            }
            return !error;
        }

        /** Initialize an unkeyed DataFrame from an existing row batch. */
        void init_unkeyed_from_rows(std::vector<CSVRow>& source_rows) {
            this->assert_fresh_storage(false);
            this->is_keyed_ = false;
            this->storage_.set_names(source_rows.empty()
                ? internals::ConstColNamesPtr(std::make_shared<internals::ColNames>())
                : source_rows.front().col_names_ptr());
            this->storage_.rows() = std::move(source_rows);
        }

        /** Initialize a keyed DataFrame from a CSV reader using column-based key extraction. */
        bool init_from_reader(CSVReader& reader, const DataFrameOptions& options, CSVError& error) {
            error.clear();
            this->assert_fresh_storage(false);
            this->is_keyed_ = true;
            this->storage_.set_names(reader.col_names_ptr());
            const std::string& name = options.get_key_column();
            if (name.empty())
                return internals::fail(error, CSVErrorCode::MissingKeyColumn, internals::ERROR_KEY_COLUMN_EMPTY);
            const int index = this->storage_.names()->index_of(name);
            if (index == CSV_NOT_FOUND)
                return internals::fail(error, CSVErrorCode::MissingKeyColumn, std::string(internals::ERROR_KEY_COLUMN_NOT_FOUND) + name);
            this->key_column_index_ = index;
            auto extract = [&options, &name](const CSVRow& row, KeyType& key, CSVError& failure) {
                CSVField field = row.try_field(name, failure);
                CSVConversionError conversion = CSVConversionError::None;
                if (!failure && field.try_get(key, conversion)) return true;
                if (!options.get_throw_on_missing_key()) {
                    failure.clear();
                    key = KeyType();
                    return true;
                }
                if (!failure) {
                    internals::fail(failure, CSVErrorCode::ConversionFailed, csv_conversion_error_message(conversion));
                    failure.conversion = conversion;
                }
                failure.context = std::string(internals::ERROR_KEY_COLUMN_VALUE) + failure.context;
                return false;
            };
            return this->build_from_key_function(reader, extract, options.get_duplicate_key_policy(), error);
        }

        template<typename KeyFunc>
        bool init_custom_from_reader(CSVReader& reader, KeyFunc& key_func, DuplicateKeyPolicy policy, CSVError& error) {
            this->storage_.set_names(reader.col_names_ptr());
            this->is_keyed_ = true;
            auto extract = [&key_func](const CSVRow& row, KeyType& key, CSVError&) {
                key = key_func(row);
                return true;
            };
            return this->build_from_key_function(reader, extract, policy, error);
        }

        /** Build keyed DataFrame using a custom key extraction function. */
        template<typename KeyFunc>
        bool build_from_key_function(
            CSVReader& reader, KeyFunc& key_func, DuplicateKeyPolicy policy, CSVError& error
        ) {
            error.clear();
            std::unordered_map<KeyType, size_t> key_to_pos;
            this->assert_fresh_storage(true);
            std::vector<CSVRow> batch;
            while (reader.try_read_chunk(batch, dataframe_read_chunk_rows(), error)) {
                if (!this->append_keyed_batch(batch, key_func, policy, key_to_pos, error)) return false;
            }
            return !error;
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
            reserve_for_append(storage_.rows(), batch.size());

            for (auto& row : batch) {
                storage_.rows().push_back(std::move(row));
            }
        }

        template<typename KeyFunc>
        bool append_keyed_batch(
            std::vector<CSVRow>& batch, KeyFunc& key_func, DuplicateKeyPolicy policy,
            std::unordered_map<KeyType, size_t>& key_to_pos, CSVError& error
        ) {
            reserve_for_append(storage_.rows(), batch.size());
            reserve_for_append(keys_, batch.size());
            for (auto& row : batch) {
                KeyType key{};
                if (!key_func(row, key, error)) return false;
                auto existing = key_to_pos.find(key);
                if (existing != key_to_pos.end()) {
                    if (policy == DuplicateKeyPolicy::THROW)
                        return internals::fail(error, CSVErrorCode::DuplicateKey, internals::ERROR_DUPLICATE_KEY);
                    if (policy == DuplicateKeyPolicy::OVERWRITE)
                        storage_.rows()[existing->second] = std::move(row);
                    continue;
                }
                storage_.rows().push_back(std::move(row));
                keys_.push_back(key);
                key_to_pos[key] = storage_.rows().size() - 1;
            }
            return true;
        }

        /** Find the index of a column by name. Throws if the column is not found. */
        size_t try_find_column(const std::string& name, CSVError& error) const {
            error.clear();
            const int index = this->index_of(name);
            if (index == CSV_NOT_FOUND) {
                internals::fail(error, CSVErrorCode::ColumnNotFound, std::string(internals::ERROR_COLUMN_NOT_FOUND) + name);
                return 0;
            }
            return static_cast<size_t>(index);
        }

        bool validate_row_index(size_t index, CSVError& error) const {
            error.clear();
            return index < this->size() || internals::fail(error, CSVErrorCode::IndexOutOfBounds, "DataFrame row index out of range");
        }
        bool validate_column_index(size_t index, CSVError& error) const {
            error.clear();
            return index < this->n_cols() || internals::fail(error, CSVErrorCode::IndexOutOfBounds, internals::ERROR_COLUMN_INDEX_OUT_OF_RANGE);
        }
        DataFrameCell try_make_cell(size_t row_index, size_t column_index, bool mutable_access, CSVError& error) const {
            const auto binding = this->storage_.try_bind(row_index, column_index, error);
            if (error) return {};
            const auto* row = &this->storage_.rows()[row_index];
            return mutable_access
                ? DataFrameCell(row, binding.overlay, binding.physical_column, &this->storage_, true)
                : DataFrameCell(row, binding.overlay, binding.physical_column);
        }

        DataFrameCell make_cell(size_t row_index, size_t column_index, bool mutable_access) const {
            return this->make_cell(&this->storage_.rows()[row_index], row_index, column_index, mutable_access);
        }

        DataFrameCell make_cell(const CSVRow* row, size_t row_index, size_t column_index, bool mutable_access) const {
            const auto binding = this->storage_.bind(row_index, column_index);
            return mutable_access
                ? DataFrameCell(row, binding.overlay, binding.physical_column, &this->storage_, true)
                : DataFrameCell(row, binding.overlay, binding.physical_column);
        }

        csv::string_view cell_view(size_t row_index, size_t column_index) const {
            return this->storage_.view(row_index, column_index);
        }

        DataFrameRow<KeyType> make_row_proxy(size_t row_index) {
            const auto& row = storage_.rows()[row_index];
            return DataFrameRow<KeyType>(&row, this, row_index, static_cast<RowOverlay*>(nullptr), this->key_ptr_at(row_index));
        }

        DataFrameRow<KeyType> make_const_row_proxy(size_t row_index) const {
            const auto& row = storage_.rows()[row_index];
            return DataFrameRow<KeyType>(&row, this, row_index, static_cast<const RowOverlay*>(nullptr), this->key_ptr_at(row_index));
        }

        bool erase_column_at_index(size_t column_index, CSVError& error) {
            if (!this->storage_.erase_column(column_index, this->key_column_index_, error)) return false;
            this->invalidate_column_proxies();
            this->invalidate_json_converter();
            return true;
        }

        bool erase_at_index(size_t row_index) {
            if (row_index >= storage_.rows().size()) {
                return false;
            }

            this->storage_.erase_row(row_index);
            if (this->is_keyed_) {
                keys_.erase(keys_.begin() + row_index);
            }
            this->invalidate_key_index();
            return true;
        }

        bool validate_selected_columns(const std::vector<size_t>& column_indices, CSVError& error) const {
            error.clear();
            for (size_t index : column_indices) {
                if (index >= this->n_cols())
                    return internals::fail(error, CSVErrorCode::IndexOutOfBounds, internals::ERROR_COLUMN_APPLY_INVALID_INDEX);
            }
            return true;
        }

        /** Validate that this DataFrame was created with a key column. */
        bool require_keyed_frame(CSVError& error) const {
            if (!this->is_keyed_)
                return internals::fail(error, CSVErrorCode::UnkeyedFrame, internals::ERROR_UNKEYED_DATA_FRAME);
            return true;
        }

        /** Invalidate the lazy key index after structural changes. */
        void invalidate_key_index() {
            key_index_.reset();
        }

        /** Invalidate cached JSON column mapping after structural column changes. */
        void invalidate_json_converter() {
            json_converter_ = internals::lazy_shared_ptr<internals::JsonConverter>();
        }

        /** Invalidate outstanding column proxies after structural column changes. */
        void invalidate_column_proxies() {
            ++(*column_generation_);
        }

        /** Debug-only check that constructor helpers are starting from a pristine batch state. */
        void assert_fresh_storage(bool expected_is_keyed) const {
            CSV_DEBUG_ASSERT(this->storage_.rows().empty());
            CSV_DEBUG_ASSERT(this->keys_.empty());
            CSV_DEBUG_ASSERT(this->storage_.pristine());
            CSV_DEBUG_ASSERT(this->key_index_.get() == nullptr);
            CSV_DEBUG_ASSERT(this->json_converter_.get() == nullptr);
            CSV_DEBUG_ASSERT(this->is_keyed_ == expected_is_keyed);
        }

        /** Build the key index if it doesn't exist (lazy initialization). */
        void ensure_key_index() const {
            if (key_index_) return;

            key_index_ = std::unique_ptr<std::unordered_map<KeyType, size_t>>(
                new std::unordered_map<KeyType, size_t>()
            );

            for (size_t i = 0; i < storage_.rows().size(); i++) {
                (*key_index_)[keys_[i]] = i;
            }
        }

        /** Find the position of a key in the rows vector. */
        size_t try_position_of(const KeyType& key, CSVError& error) const {
            error.clear();
            if (!this->require_keyed_frame(error)) return 0;
            this->ensure_key_index();
            const auto found = this->key_index_->find(key);
            if (found == this->key_index_->end()) {
                internals::fail(error, CSVErrorCode::KeyNotFound, internals::ERROR_KEY_NOT_FOUND);
                return 0;
            }
            return found->second;
        }

        const KeyType* key_ptr_at(size_t row_index) const {
            return this->is_keyed_ ? &keys_[row_index] : nullptr;
        }
    };

#if defined(CSV_HAS_CXX20) && CSV_EXCEPTIONS_ENABLED
    static_assert(
        internals::csv_write_rows_input_range<DataFrame<>>,
        "DataFrame must remain compatible with csv::DelimWriter::write_rows()."
    );
#endif
}
