#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
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
#include "json_converter.hpp"

namespace csv {
    template<typename KeyType>
    class DataFrame;
    template<typename KeyType>
    class DataFrameColumn;
    template<typename KeyType>
    class DataFrameRow;

    namespace internals {
        template<typename Owner, typename Proxy, typename Accessor>
        class indexed_proxy_iterator {
        public:
            using value_type = Proxy;
            using difference_type = std::ptrdiff_t;
            using pointer = const Proxy*;
            using reference = const Proxy&;
            using iterator_category = std::random_access_iterator_tag;

            indexed_proxy_iterator() = default;
            indexed_proxy_iterator(Owner* owner, size_t index, Accessor accessor = Accessor())
                : owner_(owner), index_(index), accessor_(accessor) {}

            reference operator*() const {
                cached_proxy_ = accessor_(owner_, index_);
                return cached_proxy_;
            }

            pointer operator->() const {
                operator*();
                return &cached_proxy_;
            }

            indexed_proxy_iterator& operator++() { ++index_; return *this; }
            indexed_proxy_iterator operator++(int) { auto tmp = *this; ++index_; return tmp; }
            indexed_proxy_iterator& operator--() { --index_; return *this; }
            indexed_proxy_iterator operator--(int) { auto tmp = *this; --index_; return tmp; }

            indexed_proxy_iterator operator+(difference_type n) const {
                return indexed_proxy_iterator(owner_, static_cast<size_t>(index_ + n), accessor_);
            }

            indexed_proxy_iterator operator-(difference_type n) const {
                return indexed_proxy_iterator(owner_, static_cast<size_t>(index_ - n), accessor_);
            }

            difference_type operator-(const indexed_proxy_iterator& other) const {
                return static_cast<difference_type>(index_) - static_cast<difference_type>(other.index_);
            }

            bool operator==(const indexed_proxy_iterator& other) const {
                return owner_ == other.owner_ && index_ == other.index_;
            }

            bool operator!=(const indexed_proxy_iterator& other) const {
                return !(*this == other);
            }

        private:
            Owner* owner_ = nullptr;
            size_t index_ = 0;
            Accessor accessor_;
            mutable Proxy cached_proxy_;
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

    /** Persistent execution backend for batch-oriented DataFrame column work. */
    class DataFrameExecutor {
    public:
        explicit DataFrameExecutor(size_t worker_count = default_worker_count()) {
            this->start_workers(worker_count);
        }

        DataFrameExecutor(const DataFrameExecutor&) = delete;
        DataFrameExecutor& operator=(const DataFrameExecutor&) = delete;

        ~DataFrameExecutor() {
            this->stop_workers();
        }

        size_t worker_count() const noexcept {
#if CSV_ENABLE_THREADS
            return workers_.size();
#else
            return 0;
#endif
        }

        template<typename Fn>
        void parallel_for(size_t task_count, Fn&& fn) {
            if (task_count == 0) {
                return;
            }

#if CSV_ENABLE_THREADS
            if (workers_.empty() || task_count == 1) {
                this->run_serial(task_count, std::forward<Fn>(fn));
                return;
            }

            {
                std::unique_lock<std::mutex> lock(mutex_);
                current_task_ = std::forward<Fn>(fn);
                next_task_.store(0);
                task_count_ = task_count;
                active_workers_ = workers_.size();
                ++generation_;
            }

            task_ready_.notify_all();

            std::unique_lock<std::mutex> lock(mutex_);
            task_done_.wait(lock, [this]() {
                return completed_generation_ == generation_;
            });

            current_task_ = std::function<void(size_t)>();
#else
            this->run_serial(task_count, std::forward<Fn>(fn));
#endif
        }

    private:
        template<typename Fn>
        void run_serial(size_t task_count, Fn&& fn) {
            for (size_t i = 0; i < task_count; ++i) {
                fn(i);
            }
        }

        static size_t default_worker_count() {
#if CSV_ENABLE_THREADS
            const unsigned int hw = std::thread::hardware_concurrency();
            return hw > 0 ? static_cast<size_t>(hw) : 1;
#else
            return 0;
#endif
        }

#if CSV_ENABLE_THREADS
        void start_workers(size_t worker_count) {
            workers_.reserve(worker_count);
            for (size_t i = 0; i < worker_count; ++i) {
                workers_.push_back(std::thread(&DataFrameExecutor::worker_loop, this));
            }
        }

        void stop_workers() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = true;
            }

            task_ready_.notify_all();
            for (auto& worker : workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
        }

        void worker_loop() {
            size_t seen_generation = 0;
            std::unique_lock<std::mutex> lock(mutex_);

            while (true) {
                task_ready_.wait(lock, [this, seen_generation]() {
                    return stop_ || generation_ != seen_generation;
                });

                if (stop_) {
                    return;
                }

                const size_t local_generation = generation_;
                seen_generation = local_generation;
                lock.unlock();

                while (true) {
                    const size_t task_index = next_task_.fetch_add(1);
                    if (task_index >= task_count_) {
                        break;
                    }

                    current_task_(task_index);
                }

                lock.lock();
                if (--active_workers_ == 0) {
                    completed_generation_ = local_generation;
                    task_done_.notify_one();
                }
            }
        }

        std::vector<std::thread> workers_;
        std::mutex mutex_;
        std::condition_variable task_ready_;
        std::condition_variable task_done_;
        std::function<void(size_t)> current_task_;
        std::atomic<size_t> next_task_{0};
        size_t task_count_ = 0;
        size_t active_workers_ = 0;
        size_t generation_ = 0;
        size_t completed_generation_ = 0;
        bool stop_ = false;
#else
        void start_workers(size_t) {}
        void stop_workers() {}
#endif
    };

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

        DataFrameCell() : CSVField(csv::string_view()), row_edits(nullptr), col_index(0), can_mutate(false) {}

        DataFrameCell(
            const CSVRow* _row,
            std::unordered_map<size_t, std::string>* _row_edits,
            size_t _col_index
        ) : CSVField(current_value(_row, _row_edits, _col_index)),
            row_edits(_row_edits),
            col_index(_col_index),
            can_mutate(true) {}

        DataFrameCell(
            const CSVRow* _row,
            const std::unordered_map<size_t, std::string>* _row_edits,
            size_t _col_index
        ) : CSVField(current_value(_row, _row_edits, _col_index)),
            row_edits(_row_edits),
            col_index(_col_index),
            can_mutate(false) {}

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
        static csv::string_view current_value(
            const CSVRow* row,
            const std::unordered_map<size_t, std::string>* row_edits,
            size_t col_index
        ) {
            if (row_edits) {
                auto it = row_edits->find(col_index);
                if (it != row_edits->end()) {
                    return csv::string_view(it->second);
                }
            }

            return (*row)[col_index].template get<csv::string_view>();
        }

        DataFrameCell& assign(std::string stored) {
            if (!can_mutate || !row_edits) {
                throw std::runtime_error("Cannot edit a const DataFrame cell.");
            }

            auto& edit_slot = const_cast<std::unordered_map<size_t, std::string>&>(*row_edits)[col_index];
            edit_slot = std::move(stored);
            CSVField::operator=(CSVField(csv::string_view(edit_slot)));
            return *this;
        }

        const std::unordered_map<size_t, std::string>* row_edits;
        size_t col_index;
        bool can_mutate;
    };

    /**
     * Proxy class that wraps a CSVRow and intercepts field access to check for edits.
     * Provides transparent access to both original and edited cell values.
     */
    template<typename KeyType>
    class DataFrameRow {
    public:
        /** Default constructor (creates an unbound proxy). */
        DataFrameRow() : row(nullptr), frame(nullptr), row_index(0), row_edits(nullptr), key_ptr(nullptr), can_mutate(false) {}

        /** Construct a mutable DataFrameRow wrapper. */
        DataFrameRow(
            const CSVRow* _row,
            DataFrame<KeyType>* _frame,
            size_t _row_index,
            std::unordered_map<size_t, std::string>* _edits,
            const KeyType* _key
        ) : row(_row), frame(_frame), row_index(_row_index), row_edits(_edits), key_ptr(_key), can_mutate(true) {}

        /** Construct a read-only DataFrameRow wrapper. */
        DataFrameRow(
            const CSVRow* _row,
            const DataFrame<KeyType>* _frame,
            size_t _row_index,
            const std::unordered_map<size_t, std::string>* _edits,
            const KeyType* _key
        ) : row(_row), frame(_frame), row_index(_row_index), row_edits(_edits), key_ptr(_key), can_mutate(false) {}

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
        std::vector<std::string> get_col_names() const { return row->get_col_names(); }

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
                throw std::runtime_error("Cannot erase a const DataFrame row.");
            }

            return const_cast<DataFrame<KeyType>*>(frame)->erase_at_index(row_index);
        }

        /** Convert to vector of strings for CSVWriter compatibility. */
        operator std::vector<std::string>() const {
            std::vector<std::string> result;
            result.reserve(row->size());
            
            for (size_t i = 0; i < row->size(); i++) {
                result.push_back(static_cast<std::string>(this->visible_value_at(i)));
            }
            return result;
        }

        /** Convert to JSON. */
        std::string to_json(const std::vector<std::string>& subset = {}) const {
            if (!frame) {
                return row->to_json(subset);
            }

            return frame->json_converter_.get_or_create([this]() {
                return std::make_shared<internals::JsonConverter>(frame->col_names_);
            }).row_to_json(
                this->size(),
                [this](size_t i) { return this->make_cell(i).get_sv(); },
                subset);
        }

        /** Convert to JSON array. */
        std::string to_json_array(const std::vector<std::string>& subset = {}) const {
            if (!frame) {
                return row->to_json_array(subset);
            }

            return frame->json_converter_.get_or_create([this]() {
                return std::make_shared<internals::JsonConverter>(frame->col_names_);
            }).row_to_json_array(
                this->size(),
                [this](size_t i) { return this->make_cell(i).get_sv(); },
                subset);
        }

        #ifdef CSV_HAS_CXX20
        /** Convert this DataFrameRow into a std::ranges::input_range of string_views,
         *  respecting the sparse overlay (edited values take precedence).
         */
        auto to_sv_range() const {
            return std::views::iota(size_t{0}, this->size())
                | std::views::transform([this](size_t i) { return this->visible_value_at(i); });
        }
        #endif

    private:
        DataFrameCell make_cell(size_t col_index) {
            return can_mutate
                ? DataFrameCell(row, const_cast<std::unordered_map<size_t, std::string>*>(row_edits), col_index)
                : DataFrameCell(row, row_edits, col_index);
        }

        DataFrameCell make_cell(size_t col_index) const {
            return DataFrameCell(row, row_edits, col_index);
        }

        csv::string_view visible_value_at(size_t col_index) const {
            if (row_edits) {
                auto it = row_edits->find(col_index);
                if (it != row_edits->end()) {
                    return csv::string_view(it->second);
                }
            }

            return (*row)[col_index].template get<csv::string_view>();
        }

        size_t find_column(const std::string& col) const {
            if (frame) {
                return frame->find_column(col);
            }

            const std::vector<std::string> col_names = row->get_col_names();
            const auto it = std::find(col_names.begin(), col_names.end(), col);
            if (it == col_names.end()) {
                throw std::out_of_range("Column not found: " + col);
            }

            return static_cast<size_t>(std::distance(col_names.begin(), it));
        }

        const CSVRow* row;
        const DataFrame<KeyType>* frame;
        size_t row_index;
        const std::unordered_map<size_t, std::string>* row_edits;
        const KeyType* key_ptr;
        bool can_mutate;
    };

    /** Lightweight non-owning view over one DataFrame column. */
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

        DataFrameColumn() : frame_(nullptr), col_index_(0) {}

        DataFrameColumn(const DataFrame<KeyType>* frame, size_t col_index)
            : frame_(frame), col_index_(col_index) {}

        /** Column name. */
        const std::string& name() const {
            return frame_->col_names_.at(col_index_);
        }

        /** Zero-based column position. */
        size_t index() const noexcept {
            return col_index_;
        }

        /** Number of rows in the parent batch. */
        size_t size() const noexcept {
            return frame_->size();
        }

        /** Access a visible cell value by row index. */
        DataFrameCell operator[](size_t row_index) const {
            const auto& row = frame_->rows.at(row_index);
            const auto* row_edits = frame_->find_row_edits(row_index);
            return DataFrameCell(&row.second, row_edits, col_index_);
        }

        /** Materialize this column as a vector of converted values. */
        template<typename T = std::string>
        std::vector<T> to_vector() const {
            std::vector<T> values;
            values.reserve(this->size());

            for (size_t row_index = 0; row_index < this->size(); ++row_index) {
                CSVField field(frame_->visible_value_at(row_index, col_index_));
                values.push_back(field.template get<T>());
            }

            return values;
        }

        /** Convert to a vector of strings. */
        operator std::vector<std::string>() const {
            return this->to_vector<std::string>();
        }

        #ifdef CSV_HAS_CXX20
        /** Convert this DataFrameColumn into a std::ranges::input_range of string_views. */
        auto to_sv_range() const {
            return std::views::iota(size_t{0}, this->size())
                | std::views::transform([this](size_t row_index) {
                    return frame_->visible_value_at(row_index, col_index_);
                });
        }
        #endif

        /** Iterate over visible cells in this column. */
        iterator begin() const { return iterator(this, 0); }
        iterator end() const { return iterator(this, this->size()); }
        const_iterator cbegin() const { return const_iterator(this, 0); }
        const_iterator cend() const { return const_iterator(this, this->size()); }

    private:
        const DataFrame<KeyType>* frame_;
        size_t col_index_;
    };

    template<typename KeyType = std::string>
    class DataFrame {
    public:
        friend class DataFrameRow<KeyType>;
        friend class DataFrameColumn<KeyType>;
        using row_type = DataFrameRow<KeyType>;
        using column_type = DataFrameColumn<KeyType>;

        /** Type alias for internal row storage: pair of key and CSVRow. */
        using row_entry = std::pair<KeyType, CSVRow>;

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
        ) : col_names_(reader.get_col_names()) {
            this->is_keyed = true;
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
        size_t n_cols() const noexcept { return col_names_.size(); }

        /** Check if a column exists in the DataFrame. */
        bool has_column(const std::string& name) const {
            return std::find(col_names_.begin(), col_names_.end(), name) != col_names_.end();
        }

        /** Get the index of a column by name. */
        int index_of(const std::string& name) const {
            auto it = std::find(col_names_.begin(), col_names_.end(), name);
            if (it == col_names_.end())
                return CSV_NOT_FOUND;
            return static_cast<int>(std::distance(col_names_.begin(), it));
        }

        /** Get the column names in order. */
        const std::vector<std::string>& columns() const noexcept {
            return col_names_;
        }

        /** Access a column view by position. */
        DataFrameColumn<KeyType> column_view(size_t col_index) const {
            if (col_index >= this->n_cols()) {
                throw std::out_of_range("Column index out of range.");
            }

            return DataFrameColumn<KeyType>(this, col_index);
        }

        /** Access a column view by name. */
        DataFrameColumn<KeyType> column_view(const std::string& name) const {
            return this->column_view(this->find_column(name));
        }

        /** Replace the current contents with a new unkeyed batch of rows.
         *
         *  Clears sparse edits and invalidates caches. Structural reset invalidates
         *  outstanding row and cell proxies.
         */
        void swap_rows(std::vector<CSVRow>& new_rows) {
            this->init_unkeyed_from_rows(new_rows);
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
            auto* row_edits = &edits[i];
            return DataFrameRow<KeyType>(&row.second, this, i, row_edits, &row.first);
        }
        
        /** Access a row by position with bounds checking (const version). */
        DataFrameRow<KeyType> at(size_t i) const {
            const auto& row = rows.at(i);
            const std::unordered_map<size_t, std::string>* row_edits = this->find_row_edits(i);
            return DataFrameRow<KeyType>(&row.second, this, i, row_edits, &row.first);
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
            return DataFrameRow<KeyType>(&rows.at(position).second, this, position, &edits[position], &rows.at(position).first);
        }

        /** Access a row by its key (const version). */
        DataFrameRow<KeyType> operator[](const KeyType& key) const {
            this->require_keyed_frame();
            auto position = this->position_of(key);
            const std::unordered_map<size_t, std::string>* row_edits = this->find_row_edits(position);
            return DataFrameRow<KeyType>(&rows.at(position).second, this, position, row_edits, &rows.at(position).first);
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
                CSVField field(this->visible_value_at(row_index, col_idx));
                values.push_back(field.template get<T>());
            }

            return values;
        }

        /** Apply a batch-oriented function to each column, potentially in parallel.
         *
         * The callback receives a lightweight column view plus a mutable per-column
         * state object from `states`.
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
                throw std::invalid_argument("column_parallel_apply() requires one state object per column.");
            }

            executor.parallel_for(this->n_cols(), [this, &states, &fn](size_t column_index) {
                fn(this->column_view(column_index), states[column_index]);
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
                grouped[std::string(this->visible_value_at(i, col_idx))].push_back(i);
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
        std::vector<std::string> col_names_;
        
        /** Internal storage: vector of (key, row) pairs. */
        std::vector<row_entry> rows;

        /** Lazily-built index mapping keys to row positions (mutable for const methods). */
        mutable std::unique_ptr<std::unordered_map<KeyType, size_t>> key_index;
        mutable internals::lazy_shared_ptr<internals::JsonConverter> json_converter_;

        /**
         * Edit overlay: row index -> column -> value.
         * Sparse storage for cell modifications without mutating original row data.
         */
        std::unordered_map<size_t, std::unordered_map<size_t, std::string>> edits;

        /** Initialize an unkeyed DataFrame from a CSV reader. */
        void init_unkeyed_from_reader(CSVReader& reader) {
            this->reset_unkeyed_state();
            this->col_names_ = reader.get_col_names();
            for (auto& row : reader) {
                rows.push_back(row_entry{KeyType(), row});
            }
        }

        /** Initialize an unkeyed DataFrame from an existing row batch. */
        void init_unkeyed_from_rows(std::vector<CSVRow>& source_rows) {
            this->reset_unkeyed_state();
            this->col_names_ = source_rows.empty() ? std::vector<std::string>() : source_rows.front().get_col_names();

            rows.reserve(source_rows.size());
            for (auto& row : source_rows) {
                rows.push_back(row_entry{KeyType(), std::move(row)});
            }

            source_rows.clear();
        }

        /** Initialize a keyed DataFrame from a CSV reader using column-based key extraction. */
        void init_from_reader(CSVReader& reader, const DataFrameOptions& options) {
            this->is_keyed = true;
            this->col_names_ = reader.get_col_names();
            const std::string key_column = options.get_key_column();

            if (key_column.empty()) {
                throw std::runtime_error("Key column cannot be empty.");
            }

            if (std::find(col_names_.begin(), col_names_.end(), key_column) == col_names_.end()) {
                throw std::runtime_error("Key column not found: " + key_column);
            }

            const bool throw_on_missing_key = options.get_throw_on_missing_key();

            this->build_from_key_function(
                reader,
                [key_column, throw_on_missing_key](const CSVRow& row) -> KeyType {
                    try {
                        return row[key_column].template get<KeyType>();
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

        /** Find the index of a column by name. Throws if the column is not found. */
        size_t find_column(const std::string& name) const {
            return index_of(name) != CSV_NOT_FOUND ? static_cast<size_t>(index_of(name)) :
                throw std::out_of_range("Column not found: " + name);
        }

        /** Find the edits for a specific row index. Returns nullptr if no edits exist for the row. */
        const std::unordered_map<size_t, std::string>* find_row_edits(size_t row_index) const {
            auto it = edits.find(row_index);
            return it != edits.end() ? &it->second : nullptr;
        }

        DataFrameRow<KeyType> make_row_proxy(size_t row_index) {
            const auto& row = rows.at(row_index);
            return DataFrameRow<KeyType>(&row.second, this, row_index, &edits[row_index], &row.first);
        }

        DataFrameRow<KeyType> make_const_row_proxy(size_t row_index) const {
            const auto& row = rows.at(row_index);
            return DataFrameRow<KeyType>(&row.second, this, row_index, this->find_row_edits(row_index), &row.first);
        }

        csv::string_view visible_value_at(size_t row_index, size_t col_index) const {
            const auto* row_edits = this->find_row_edits(row_index);
            if (row_edits) {
                auto edited_value = row_edits->find(col_index);
                if (edited_value != row_edits->end()) {
                    return csv::string_view(edited_value->second);
                }
            }

            return rows[row_index].second[col_index].template get<csv::string_view>();
        }

        void erase_row_edits(size_t row_index) {
            if (edits.empty()) {
                return;
            }

            std::unordered_map<size_t, std::unordered_map<size_t, std::string>> shifted_edits;
            shifted_edits.reserve(edits.size());

            for (auto& entry : edits) {
                if (entry.first == row_index) {
                    continue;
                }

                const size_t target_index = entry.first > row_index ? entry.first - 1 : entry.first;
                shifted_edits.emplace(target_index, std::move(entry.second));
            }

            edits = std::move(shifted_edits);
        }

        bool erase_at_index(size_t row_index) {
            if (row_index >= rows.size()) {
                return false;
            }

            this->erase_row_edits(row_index);
            rows.erase(rows.begin() + row_index);
            this->invalidate_key_index();
            return true;
        }

        /** Validate that this DataFrame was created with a key column. */
        void require_keyed_frame() const {
            if (!is_keyed) {
                throw std::runtime_error("This DataFrame was created without a key column.");
            }
        }

        /** Reset state that should not survive replacing the current batch. */
        void reset_unkeyed_state() {
            this->is_keyed = false;
            this->rows.clear();
            this->edits.clear();
            this->invalidate_key_index();
            this->json_converter_ = internals::lazy_shared_ptr<internals::JsonConverter>();
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

    #ifdef CSV_HAS_CXX20
    static_assert(
        internals::csv_write_rows_input_range<DataFrame<>>,
        "DataFrame must remain compatible with csv::DelimWriter::write_rows()."
    );
    #endif
}
