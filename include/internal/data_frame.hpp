#pragma once

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "csv_reader.hpp"

namespace csv {
    namespace internals {
        template<typename T>
        class is_hashable {
        private:
            template<typename U>
            static auto test(int) -> decltype(
                std::hash<U>{}(std::declval<const U&>()),
                std::true_type{}
            );

            template<typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template<typename T>
        class is_equality_comparable {
        private:
            template<typename U>
            static auto test(int) -> decltype(
                std::declval<const U&>() == std::declval<const U&>(),
                std::true_type{}
            );

            template<typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };
    }

    /** Allows configuration of DataFrame behavior. */
    class DataFrameOptions {
    public:
        DataFrameOptions() = default;

        /** Policy for handling duplicate keys when creating a keyed DataFrame */
        enum class DuplicateKeyPolicy {
            THROW,      // Throw an error if a duplicate key is encountered
            OVERWRITE,  // Overwrite the existing value with the new value
            KEEP_FIRST  // Ignore the new value and keep the existing value
        };

        DataFrameOptions& set_duplicate_key_policy(DuplicateKeyPolicy value) {
            this->duplicate_key_policy = value;
            return *this;
        }

        DuplicateKeyPolicy get_duplicate_key_policy() const {
            return this->duplicate_key_policy;
        }

        DataFrameOptions& set_key_column(const std::string& value) {
            this->key_column = value;
            return *this;
        }

        const std::string& get_key_column() const {
            return this->key_column;
        }

        DataFrameOptions& set_throw_on_missing_key(bool value) {
            this->throw_on_missing_key = value;
            return *this;
        }

        bool get_throw_on_missing_key() const {
            return this->throw_on_missing_key;
        }

    private:
        std::string key_column;

        DuplicateKeyPolicy duplicate_key_policy = DuplicateKeyPolicy::OVERWRITE;

        /** Whether to throw an error if a key column value is missing when creating a keyed DataFrame */
        bool throw_on_missing_key = true;
    };

    /**
     * Proxy class that wraps a CSVRow and intercepts field access to check for edits.
     * Provides transparent access to both original and edited cell values.
     */
    template<typename KeyType>
    class DataFrameRow {
    public:
        /** Default constructor (creates an unbound proxy). */
        DataFrameRow() : row(nullptr), row_edits(nullptr), key_ptr(nullptr) {}

        /** Construct a DataFrameRow wrapper. */
        DataFrameRow(
            const CSVRow* _row,
            const std::unordered_map<std::string, std::string>* _edits,
            const KeyType* _key
        ) : row(_row), row_edits(_edits), key_ptr(_key) {}

        /**
         * Access a field by column name, checking edits first.
         * 
         * @param col Column name
         * @return CSVField representing the field value (edited if present, otherwise original)
         */
        CSVField operator[](const std::string& col) const {
            if (row_edits) {
                auto it = row_edits->find(col);
                if (it != row_edits->end()) {
                    return CSVField(csv::string_view(it->second));
                }
            }
            return (*row)[col];
        }

        /** Access a field by position (positional access never checks edits). */
        CSVField operator[](size_t n) const {
            return (*row)[n];
        }

        /** Get the number of fields in the row. */
        size_t size() const { return row->size(); }

        /** Check if the row is empty. */
        bool empty() const { return row->empty(); }

        /** Get column names. */
        std::vector<std::string> get_col_names() const { return row->get_col_names(); }

        /** Get the underlying CSVRow for compatibility. */
        const CSVRow& get_underlying_row() const { return *row; }

        /** Get the key for this row (only valid for keyed DataFrames). */
        const KeyType& get_key() const { return *key_ptr; }

        /** Convert to vector of strings for CSVWriter compatibility. */
        operator std::vector<std::string>() const {
            std::vector<std::string> result;
            result.reserve(row->size());
            
            auto col_names = row->get_col_names();
            for (size_t i = 0; i < row->size(); i++) {
                // Check if this column has an edit
                if (row_edits && i < col_names.size()) {
                    auto it = row_edits->find(col_names[i]);
                    if (it != row_edits->end()) {
                        result.push_back(it->second);
                        continue;
                    }
                }
                // Use original value
                result.push_back((*row)[i].get<std::string>());
            }
            return result;
        }

        /** Convert to JSON. */
        std::string to_json(const std::vector<std::string>& subset = {}) const {
            return row->to_json(subset);
        }

        /** Convert to JSON array. */
        std::string to_json_array(const std::vector<std::string>& subset = {}) const {
            return row->to_json_array(subset);
        }

    private:
        const CSVRow* row;
        const std::unordered_map<std::string, std::string>* row_edits;
        const KeyType* key_ptr;
    };

    template<typename KeyType = std::string>
    class DataFrame {
    public:
        /** Type alias for internal row storage: pair of key and CSVRow. */
        using row_entry = std::pair<KeyType, CSVRow>;

        /** Row-wise iterator over DataFrameRow entries. Provides access to rows with edit support. */
        class iterator {
        public:
            using value_type = DataFrameRow<KeyType>;
            using difference_type = std::ptrdiff_t;
            using pointer = const DataFrameRow<KeyType>*;
            using reference = const DataFrameRow<KeyType>&;
            using iterator_category = std::random_access_iterator_tag;

            iterator() = default;
            iterator(
                typename std::vector<row_entry>::iterator it,
                const std::unordered_map<KeyType, std::unordered_map<std::string, std::string>>* edits
            ) : iter(it), edits_map(edits) {}

            reference operator*() const {
                const std::unordered_map<std::string, std::string>* row_edits = nullptr;
                if (edits_map) {
                    auto it = edits_map->find(iter->first);
                    if (it != edits_map->end()) {
                        row_edits = &it->second;
                    }
                }
                cached_row = DataFrameRow<KeyType>(&iter->second, row_edits, &iter->first);
                return cached_row;
            }

            pointer operator->() const {
                // Ensure cached_row is populated
                operator*();
                return &cached_row;
            }

            iterator& operator++() { ++iter; return *this; }
            iterator operator++(int) { auto tmp = *this; ++iter; return tmp; }
            iterator& operator--() { --iter; return *this; }
            iterator operator--(int) { auto tmp = *this; --iter; return tmp; }

            iterator operator+(difference_type n) const { return iterator(iter + n, edits_map); }
            iterator operator-(difference_type n) const { return iterator(iter - n, edits_map); }
            difference_type operator-(const iterator& other) const { return iter - other.iter; }

            bool operator==(const iterator& other) const { return iter == other.iter; }
            bool operator!=(const iterator& other) const { return iter != other.iter; }

        private:
            typename std::vector<row_entry>::iterator iter;
            const std::unordered_map<KeyType, std::unordered_map<std::string, std::string>>* edits_map = nullptr;
            mutable DataFrameRow<KeyType> cached_row;
        };

        /** Row-wise const iterator over DataFrameRow entries. Provides read-only access to rows with edit support. */
        class const_iterator {
        public:
            using value_type = DataFrameRow<KeyType>;
            using difference_type = std::ptrdiff_t;
            using pointer = const DataFrameRow<KeyType>*;
            using reference = const DataFrameRow<KeyType>&;
            using iterator_category = std::random_access_iterator_tag;

            const_iterator() = default;
            const_iterator(
                typename std::vector<row_entry>::const_iterator it,
                const std::unordered_map<KeyType, std::unordered_map<std::string, std::string>>* edits
            ) : iter(it), edits_map(edits) {}

            reference operator*() const {
                const std::unordered_map<std::string, std::string>* row_edits = nullptr;
                if (edits_map) {
                    auto it = edits_map->find(iter->first);
                    if (it != edits_map->end()) {
                        row_edits = &it->second;
                    }
                }
                cached_row = DataFrameRow<KeyType>(&iter->second, row_edits, &iter->first);
                return cached_row;
            }

            pointer operator->() const {
                // Ensure cached_row is populated
                operator*();
                return &cached_row;
            }

            const_iterator& operator++() { ++iter; return *this; }
            const_iterator operator++(int) { auto tmp = *this; ++iter; return tmp; }
            const_iterator& operator--() { --iter; return *this; }
            const_iterator operator--(int) { auto tmp = *this; --iter; return tmp; }

            const_iterator operator+(difference_type n) const { return const_iterator(iter + n, edits_map); }
            const_iterator operator-(difference_type n) const { return const_iterator(iter - n, edits_map); }
            difference_type operator-(const const_iterator& other) const { return iter - other.iter; }

            bool operator==(const const_iterator& other) const { return iter == other.iter; }
            bool operator!=(const const_iterator& other) const { return iter != other.iter; }

        private:
            typename std::vector<row_entry>::const_iterator iter;
            const std::unordered_map<KeyType, std::unordered_map<std::string, std::string>>* edits_map = nullptr;
            mutable DataFrameRow<KeyType> cached_row;
        };

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

        /**
         * Construct a keyed DataFrame from a CSV reader with options.
         * 
         * @param reader CSV reader to consume
         * @param options Configuration including key column and duplicate policies
         * @throws std::runtime_error if key column is empty or not found
         */
        explicit DataFrame(CSVReader& reader, const DataFrameOptions& options) {
            this->init_from_reader(reader, options);
        }

        /**
         * Construct a keyed DataFrame directly from a CSV file.
         * 
         * @param filename Path to the CSV file
         * @param options Configuration including key column and duplicate policies
         * @param format CSV format specification (defaults to auto-detection)
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

        /**
         * Construct a keyed DataFrame using a column name as the key.
         * 
         * @param reader CSV reader to consume
         * @param _key_column Name of the column to use as the key
         * @param policy How to handle duplicate keys (default: OVERWRITE)
         * @param throw_on_missing_key Whether to throw if a key value cannot be parsed (default: true)
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

        /**
         * Construct a keyed DataFrame using a custom key function.
         * 
         * @param reader CSV reader to consume
         * @param key_func Function that extracts a key from each row
         * @param policy How to handle duplicate keys (default: OVERWRITE)
         * @throws std::runtime_error if policy is THROW and duplicate keys are encountered
         */
        template<
            typename KeyFunc,
            typename ResultType = invoke_result_t<KeyFunc, const CSVRow&>,
            csv::enable_if_t<std::is_convertible<ResultType, KeyType>::value, int> = 0
        >
        DataFrame(
            CSVReader& reader,
            KeyFunc key_func,
            DuplicateKeyPolicy policy = DuplicateKeyPolicy::OVERWRITE
        ) : col_names(reader.get_col_names()) {
            this->is_keyed = true;
            this->build_from_key_function(reader, key_func, policy);
        }

        /**
         * Construct a keyed DataFrame using a custom key function with options.
         * 
         * @param reader CSV reader to consume
         * @param key_func Function that extracts a key from each row
         * @param options Configuration for duplicate key policy
         */
        template<
            typename KeyFunc,
            typename ResultType = invoke_result_t<KeyFunc, const CSVRow&>,
            csv::enable_if_t<std::is_convertible<ResultType, KeyType>::value, int> = 0
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
        size_t n_cols() const noexcept { return col_names.size(); }

        /**
         * Check if a column exists in the DataFrame.
         * 
         * @param name Column name to check
         * @return true if the column exists, false otherwise
         */
        bool has_column(const std::string& name) const {
            return std::find(col_names.begin(), col_names.end(), name) != col_names.end();
        }

        /**
         * Get the index of a column by name.
         * 
         * @param name Column name to find
         * @return Column index (0-based) or CSV_NOT_FOUND if not found
         */
        int index_of(const std::string& name) const {
            auto it = std::find(col_names.begin(), col_names.end(), name);
            if (it == col_names.end())
                return CSV_NOT_FOUND;
            return static_cast<int>(std::distance(col_names.begin(), it));
        }

        /** Get the column names in order. */
        const std::vector<std::string>& columns() const noexcept {
            return col_names;
        }

        /** Get the name of the key column (empty string if unkeyed). */
        const std::string& key_name() const noexcept {
            return key_column;
        }

        /**
         * Access a row by position (unchecked).
         * 
         * @param i Row index (0-based)
         * @return DataFrameRow proxy with edit support
         * @throws std::out_of_range if index is out of bounds (via std::vector::at)
         */
        DataFrameRow<KeyType> operator[](size_t i) {
            return this->iloc(i);
        }

        /** Access a row by position (unchecked, const version). */
        DataFrameRow<KeyType> operator[](size_t i) const {
            return this->iloc(i);
        }

        /**
         * Access a row by position with bounds checking.
         * 
         * @param i Row index (0-based)
         * @return DataFrameRow proxy with edit support
         * @throws std::out_of_range if index is out of bounds
         */
        DataFrameRow<KeyType> at(size_t i) {
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            if (is_keyed) {
                auto it = edits.find(rows.at(i).first);
                if (it != edits.end()) row_edits = &it->second;
            }
            return DataFrameRow<KeyType>(&rows.at(i).second, row_edits, &rows.at(i).first);
        }
        
        /** Access a row by position with bounds checking (const version). */
        DataFrameRow<KeyType> at(size_t i) const {
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            if (is_keyed) {
                auto it = edits.find(rows.at(i).first);
                if (it != edits.end()) row_edits = &it->second;
            }
            return DataFrameRow<KeyType>(&rows.at(i).second, row_edits, &rows.at(i).first);
        }

        /**
         * Access a row by its key.
         * 
         * @param key The row key to look up
         * @return DataFrameRow proxy with edit support
         * @throws std::runtime_error if the DataFrame was not created with a key column
         * @throws std::out_of_range if the key is not found
         */
        DataFrameRow<KeyType> operator[](const KeyType& key) {
            this->require_keyed_frame();
            auto position = this->position_of(key);
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            auto it = edits.find(key);
            if (it != edits.end()) row_edits = &it->second;
            return DataFrameRow<KeyType>(&rows[position].second, row_edits, &rows[position].first);
        }

        /** Access a row by its key (const version). */
        DataFrameRow<KeyType> operator[](const KeyType& key) const {
            this->require_keyed_frame();
            auto position = this->position_of(key);
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            auto it = edits.find(key);
            if (it != edits.end()) row_edits = &it->second;
            return DataFrameRow<KeyType>(&rows[position].second, row_edits, &rows[position].first);
        }

        /**
         * Access a row by position (iloc-style, pandas naming).
         * 
         * @param i Row index (0-based)
         * @return DataFrameRow proxy with edit support
         * @throws std::out_of_range if index is out of bounds
         */
        DataFrameRow<KeyType> iloc(size_t i) {
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            if (is_keyed) {
                auto it = edits.find(rows.at(i).first);
                if (it != edits.end()) row_edits = &it->second;
            }
            return DataFrameRow<KeyType>(&rows.at(i).second, row_edits, &rows.at(i).first);
        }

        /** Access a row by position (const version). */
        DataFrameRow<KeyType> iloc(size_t i) const {
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            if (is_keyed) {
                auto it = edits.find(rows.at(i).first);
                if (it != edits.end()) row_edits = &it->second;
            }
            return DataFrameRow<KeyType>(&rows.at(i).second, row_edits, &rows.at(i).first);
        }

        /**
         * Attempt to access a row by position without throwing.
         * 
         * @param i Row index (0-based)
         * @param out Output parameter that receives the DataFrameRow if found
         * @return true if the row exists, false otherwise
         */
        bool try_get(size_t i, DataFrameRow<KeyType>& out) {
            if (i >= rows.size()) {
                return false;
            }
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            if (is_keyed) {
                auto it = edits.find(rows[i].first);
                if (it != edits.end()) row_edits = &it->second;
            }
            out = DataFrameRow<KeyType>(&rows[i].second, row_edits, &rows[i].first);
            return true;
        }

        /** Attempt to access a row by position without throwing (const version). */
        bool try_get(size_t i, DataFrameRow<KeyType>& out) const {
            if (i >= rows.size()) {
                return false;
            }
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            if (is_keyed) {
                auto it = edits.find(rows[i].first);
                if (it != edits.end()) row_edits = &it->second;
            }
            out = DataFrameRow<KeyType>(&rows[i].second, row_edits, &rows[i].first);
            return true;
        }

        /**
         * Get the key for a row at a given position.
         * 
         * @param i Row index (0-based)
         * @return Reference to the key
         * @throws std::runtime_error if the DataFrame was not created with a key column
         * @throws std::out_of_range if index is out of bounds
         */
        const KeyType& key_at(size_t i) const {
            this->require_keyed_frame();
            return rows.at(i).first;
        }

        /**
         * Check if a key exists in the DataFrame.
         * 
         * @param key The key to check
         * @return true if the key exists, false otherwise
         * @throws std::runtime_error if the DataFrame was not created with a key column
         */
        bool contains(const KeyType& key) const {
            this->require_keyed_frame();
            this->ensure_key_index();
            return key_index->find(key) != key_index->end();
        }

        /**
         * Access a row by its key with bounds checking.
         * 
         * @param key The row key to look up
         * @return DataFrameRow proxy with edit support
         * @throws std::runtime_error if the DataFrame was not created with a key column
         * @throws std::out_of_range if the key is not found
         */
        DataFrameRow<KeyType> at(const KeyType& key) {
            this->require_keyed_frame();
            auto position = this->position_of(key);
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            auto it = edits.find(key);
            if (it != edits.end()) row_edits = &it->second;
            return DataFrameRow<KeyType>(&rows.at(position).second, row_edits, &rows.at(position).first);
        }

        /** Access a row by its key with bounds checking (const version). */
        DataFrameRow<KeyType> at(const KeyType& key) const {
            this->require_keyed_frame();
            auto position = this->position_of(key);
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            auto it = edits.find(key);
            if (it != edits.end()) row_edits = &it->second;
            return DataFrameRow<KeyType>(&rows.at(position).second, row_edits, &rows.at(position).first);
        }

        /**
         * Attempt to access a row by key without throwing.
         * 
         * @param key The row key to look up
         * @param out Output parameter that receives the DataFrameRow if found
         * @return true if the key exists, false otherwise
         * @throws std::runtime_error if the DataFrame was not created with a key column
         */
        bool try_get(const KeyType& key, DataFrameRow<KeyType>& out) {
            this->require_keyed_frame();
            this->ensure_key_index();
            auto it = key_index->find(key);
            if (it == key_index->end()) {
                return false;
            }
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            auto edit_it = edits.find(key);
            if (edit_it != edits.end()) row_edits = &edit_it->second;
            out = DataFrameRow<KeyType>(&rows[it->second].second, row_edits, &rows[it->second].first);
            return true;
        }

        /** Attempt to access a row by key without throwing (const version). */
        bool try_get(const KeyType& key, DataFrameRow<KeyType>& out) const {
            this->require_keyed_frame();
            this->ensure_key_index();
            auto it = key_index->find(key);
            if (it == key_index->end()) {
                return false;
            }
            const std::unordered_map<std::string, std::string>* row_edits = nullptr;
            auto edit_it = edits.find(key);
            if (edit_it != edits.end()) row_edits = &edit_it->second;
            out = DataFrameRow<KeyType>(&rows[it->second].second, row_edits, &rows[it->second].first);
            return true;
        }

        /**
         * Get a cell value as a string, accounting for edits.
         * 
         * @param key The row key
         * @param column The column name
         * @return Cell value as a string (edited value if present, otherwise original)
         * @throws std::runtime_error if the DataFrame was not created with a key column
         * @throws std::out_of_range if the key is not found
         */
        std::string get(const KeyType& key, const std::string& column) const {
            this->require_keyed_frame();

            auto row_edits = this->edits.find(key);
            if (row_edits != this->edits.end()) {
                auto value = row_edits->second.find(column);
                if (value != row_edits->second.end()) {
                    return value->second;
                }
            }

            return (*this)[key][column].template get<std::string>();
        }

        /**
         * Set a cell value (stored in edit overlay).
         * 
         * @param key The row key
         * @param column The column name
         * @param value The new value as a string
         * @throws std::runtime_error if the DataFrame was not created with a key column
         * @throws std::out_of_range if the key is not found
         */
        void set(const KeyType& key, const std::string& column, const std::string& value) {
            this->require_keyed_frame();
            (void)this->position_of(key);
            edits[key][column] = value;
        }

        /**
         * Remove a row by its key.
         * 
         * @param key The row key to remove
         * @return true if the row was removed, false if not found
         * @throws std::runtime_error if the DataFrame was not created with a key column
         */
        bool erase_row(const KeyType& key) {
            this->require_keyed_frame();
            this->ensure_key_index();

            auto it = key_index->find(key);
            if (it == key_index->end()) {
                return false;
            }

            rows.erase(rows.begin() + it->second);
            edits.erase(key);
            this->invalidate_key_index();
            return true;
        }

        /**
         * Remove a row by its position.
         * 
         * @param i Row index (0-based)
         * @return true if the row was removed, false if index out of bounds
         */
        bool erase_row_at(size_t i) {
            if (i >= rows.size()) return false;
            if (is_keyed) edits.erase(rows[i].first);

            rows.erase(rows.begin() + i);
            this->invalidate_key_index();
            return true;
        }

        /**
         * Set a cell value by position (stored in edit overlay).
         * 
         * @param i Row index (0-based)
         * @param column The column name
         * @param value The new value as a string
         * @throws std::runtime_error if the DataFrame was not created with a key column
         * @throws std::out_of_range if index is out of bounds
         */
        void set_at(size_t i, const std::string& column, const std::string& value) {
            if (!is_keyed) {
                throw std::runtime_error("This DataFrame was created without a key column.");
            }
            if (i >= rows.size()) {
                throw std::out_of_range("Row index out of bounds.");
            }
            edits[rows[i].first][column] = value;
        }

        /**
         * Extract all values from a column with type conversion.
         * Accounts for edited values in the overlay.
         * 
         * @tparam T Target type for conversion (default: std::string)
         * @param name Column name
         * @return Vector of values converted to type T
         * @throws std::runtime_error if column is not found
         */
        template<typename T = std::string>
        std::vector<T> column(const std::string& name) const {
            if (std::find(col_names.begin(), col_names.end(), name) == col_names.end()) {
                throw std::runtime_error("Column not found: " + name);
            }

            std::vector<T> values;
            values.reserve(rows.size());

            for (const auto& entry : rows) {
                auto row_edits = this->edits.find(entry.first);
                if (row_edits != this->edits.end()) {
                    auto value = row_edits->second.find(name);
                    if (value != row_edits->second.end()) {
                        // Reuse CSVField parsing/validation on edited string values.
                        CSVField edited_field(csv::string_view(value->second));
                        values.push_back(edited_field.template get<T>());
                        continue;
                    }
                }

                values.push_back(entry.second[name].template get<T>());
            }

            return values;
        }

        /**
         * Group row positions using an arbitrary grouping function.
         * 
         * @tparam GroupFunc Callable that takes a CSVRow and returns a hashable key
         * @param group_func Function to extract group key from each row
         * @return Map of group key -> vector of row indices belonging to that group
         */
        template<
            typename GroupFunc,
            typename GroupKey = invoke_result_t<GroupFunc, const CSVRow&>,
            csv::enable_if_t<
                internals::is_hashable<GroupKey>::value &&
                internals::is_equality_comparable<GroupKey>::value,
                int
            > = 0
        >
        std::unordered_map<GroupKey, std::vector<size_t>> group_by(GroupFunc group_func) const {
            std::unordered_map<GroupKey, std::vector<size_t>> grouped;

            for (size_t i = 0; i < rows.size(); i++) {
                GroupKey group_key = group_func(rows[i].second);
                grouped[group_key].push_back(i);
            }

            return grouped;
        }

        /**
         * Group row positions by the value of a column.
         * 
         * @param name Column to group by
         * @param use_edits If true, use edited values when present (default: true)
         * @return Map of column value -> vector of row indices with that value
         * @throws std::runtime_error if column is not found
         */
        std::unordered_map<std::string, std::vector<size_t>> group_by(
            const std::string& name,
            bool use_edits = true
        ) const {
            if (std::find(col_names.begin(), col_names.end(), name) == col_names.end()) {
                throw std::runtime_error("Column not found: " + name);
            }

            std::unordered_map<std::string, std::vector<size_t>> grouped;

            for (size_t i = 0; i < rows.size(); i++) {
                std::string group_key;
                bool has_group_key = false;

                if (use_edits) {
                    auto row_edits = this->edits.find(rows[i].first);
                    if (row_edits != this->edits.end()) {
                        auto edited_value = row_edits->second.find(name);
                        if (edited_value != row_edits->second.end()) {
                            group_key = edited_value->second;
                            has_group_key = true;
                        }
                    }
                }

                if (!has_group_key) {
                    group_key = rows[i].second[name].template get<std::string>();
                }

                grouped[group_key].push_back(i);
            }

            return grouped;
        }

        /** Get iterator to the first row. */
        iterator begin() { return iterator(rows.begin(), is_keyed ? &edits : nullptr); }
        
        /** Get iterator past the last row. */
        iterator end() { return iterator(rows.end(), is_keyed ? &edits : nullptr); }
        
        /** Get const iterator to the first row. */
        const_iterator begin() const { return const_iterator(rows.begin(), is_keyed ? &edits : nullptr); }
        
        /** Get const iterator past the last row. */
        const_iterator end() const { return const_iterator(rows.end(), is_keyed ? &edits : nullptr); }
        
        /** Get const iterator to the first row (explicit). */
        const_iterator cbegin() const { return const_iterator(rows.begin(), is_keyed ? &edits : nullptr); }
        
        /** Get const iterator past the last row (explicit). */
        const_iterator cend() const { return const_iterator(rows.end(), is_keyed ? &edits : nullptr); }

    private:
        /** Name of the key column (empty if unkeyed). */
        std::string key_column;
        
        /** Whether this DataFrame was created with a key. */
        bool is_keyed = false;
        
        /** Column names in order. */
        std::vector<std::string> col_names;
        
        /** Internal storage: vector of (key, row) pairs. */
        std::vector<row_entry> rows;

        /** Lazily-built index mapping keys to row positions (mutable for const methods). */
        mutable std::unique_ptr<std::unordered_map<KeyType, size_t>> key_index;

        /**
         * Edit overlay: key -> column -> value.
         * Sparse storage for cell modifications without mutating original row data.
         */
        std::unordered_map<KeyType, std::unordered_map<std::string, std::string>> edits;

        /** Initialize an unkeyed DataFrame from a CSV reader. */
        void init_unkeyed_from_reader(CSVReader& reader) {
            this->col_names = reader.get_col_names();
            for (auto& row : reader) {
                rows.push_back(row_entry{KeyType(), row});
            }
        }

        /** Initialize a keyed DataFrame from a CSV reader using column-based key extraction. */
        void init_from_reader(CSVReader& reader, const DataFrameOptions& options) {
            this->is_keyed = true;
            this->key_column = options.get_key_column();
            this->col_names = reader.get_col_names();

            if (key_column.empty()) {
                throw std::runtime_error("Key column cannot be empty.");
            }

            if (std::find(col_names.begin(), col_names.end(), key_column) == col_names.end()) {
                throw std::runtime_error("Key column not found: " + key_column);
            }

            const bool throw_on_missing_key = options.get_throw_on_missing_key();

            this->build_from_key_function(
                reader,
                [this, throw_on_missing_key](const CSVRow& row) -> KeyType {
                    try {
                        return row[this->key_column].template get<KeyType>();
                    }
                    catch (const std::exception& e) {
                        if (throw_on_missing_key) {
                            throw std::runtime_error("Error retrieving key column value: " + std::string(e.what()));
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

            for (auto& row : reader) {
                KeyType key = key_func(row);

                auto existing = key_to_pos.find(key);
                if (existing != key_to_pos.end()) {
                    if (policy == DuplicateKeyPolicy::THROW) {
                        throw std::runtime_error("Duplicate key encountered.");
                    }

                    if (policy == DuplicateKeyPolicy::OVERWRITE) {
                        rows[existing->second].second = row;
                    }

                    continue;
                }

                rows.push_back(row_entry{key, row});
                key_to_pos[key] = rows.size() - 1;
            }
        }

        /** Validate that this DataFrame was created with a key column. */
        void require_keyed_frame() const {
            if (!is_keyed) {
                throw std::runtime_error("This DataFrame was created without a key column.");
            }
        }

        /** Invalidate the lazy key index after structural changes. */
        void invalidate_key_index() {
            key_index.reset();
        }

        /** Build the key index if it doesn't exist (lazy initialization). */
        void ensure_key_index() const {
            if (key_index) {
                return;
            }

            key_index = std::unique_ptr<std::unordered_map<KeyType, size_t>>(
                new std::unordered_map<KeyType, size_t>()
            );

            for (size_t i = 0; i < rows.size(); i++) {
                (*key_index)[rows[i].first] = i;
            }
        }

        /** Find the position of a key in the rows vector. */
        size_t position_of(const KeyType& key) const {
            this->ensure_key_index();
            auto it = key_index->find(key);
            if (it == key_index->end()) {
                throw std::out_of_range("Key not found.");
            }

            return it->second;
        }
    };
}