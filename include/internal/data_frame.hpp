#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
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

    struct RowOverlay {
        RowOverlay() = default;
        RowOverlay(const RowOverlay&) = delete;
        RowOverlay& operator=(const RowOverlay&) = delete;

        RowOverlay(RowOverlay&& other) noexcept : values(std::move(other.values)) {
            busy.clear(std::memory_order_release);
        }

        RowOverlay& operator=(RowOverlay&& other) noexcept {
            if (this != &other) {
                values = std::move(other.values);
                busy.clear(std::memory_order_release);
            }
            return *this;
        }

        bool try_get_copy(size_t col_index, std::string& out) const {
            row_overlay_lock_guard lock(this);
            auto it = values.find(col_index);
            if (it == values.end()) {
                return false;
            }

            out = it->second;
            return true;
        }

        void set(size_t col_index, std::string value) {
            row_overlay_lock_guard lock(this);
            values[col_index] = std::move(value);
        }

        bool empty() const {
            row_overlay_lock_guard lock(this);
            return values.empty();
        }

    private:
        struct row_overlay_lock_guard {
            explicit row_overlay_lock_guard(const RowOverlay* overlay)
                : busy(const_cast<std::atomic_flag&>(overlay->busy)) {
                while (busy.test_and_set(std::memory_order_acquire)) {}
            }

            ~row_overlay_lock_guard() {
                busy.clear(std::memory_order_release);
            }

            std::atomic_flag& busy;
        };

        mutable std::atomic_flag busy = ATOMIC_FLAG_INIT;
        std::unordered_map<size_t, std::string> values;
    };

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
            if (workers_.empty() || task_count <= workers_.size()) {
                this->run_serial(task_count, std::forward<Fn>(fn));
                return;
            }

            std::exception_ptr captured_exception;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                current_task_ = std::forward<Fn>(fn);
                task_exception_ = nullptr;
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

            captured_exception = task_exception_;
            current_task_ = std::function<void(size_t)>();
            task_exception_ = nullptr;

            if (captured_exception) {
                std::rethrow_exception(captured_exception);
            }
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
                if (worker.joinable())
                    worker.join();
            }
        }

        void worker_loop() {
            size_t seen_generation = 0;
            std::unique_lock<std::mutex> lock(mutex_);

            while (true) {
                task_ready_.wait(lock, [this, seen_generation]() {
                    return stop_ || generation_ != seen_generation;
                });

                if (stop_) return;

                const size_t local_generation = generation_;
                seen_generation = local_generation;
                lock.unlock();

                while (true) {
                    const size_t task_index = next_task_.fetch_add(1);
                    if (task_index >= task_count_)
                        break;

                    try {
                        current_task_(task_index);
                    }
                    catch (...) {
                        lock.lock();
                        if (!task_exception_) {
                            task_exception_ = std::current_exception();
                            next_task_.store(task_count_);
                        }
                        lock.unlock();
                        break;
                    }
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
        std::exception_ptr task_exception_ = nullptr;
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
                throw std::runtime_error("Cannot edit a const DataFrame cell.");
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

    /**
     * Proxy class that wraps a CSVRow and intercepts field access to check for edits.
     * Provides transparent access to both original and edited cell values.
     */
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
                throw std::runtime_error("Cannot erase a const DataFrame row.");
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
                throw std::out_of_range("Column not found: " + col);
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
            return (*frame_->col_names_)[col_index_];
        }

        /** Zero-based column position. */
        size_t index() const noexcept {
            return col_index_;
        }

        /** Number of rows in the parent batch. */
        size_t size() const noexcept {
            return frame_->size();
        }

        /** Whether the parent batch contains no rows. */
        bool empty() const noexcept {
            return this->size() == 0;
        }

        /** Access a visible cell value by row index. */
        DataFrameCell operator[](size_t row_index) const {
            const auto& row = frame_->rows.at(row_index);
            const auto* row_edits = frame_->find_row_edits(row_index);
            return DataFrameCell(&row, row_edits, col_index_);
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

        #ifdef CSV_HAS_CXX20
        /** Convert this DataFrameColumn into a std::ranges::input_range of strings. */
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
            auto* row_edits = &edits.at(i);
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
            return DataFrameRow<KeyType>(&rows.at(position), this, position, &edits.at(position), this->key_ptr_at(position));
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
                throw std::invalid_argument("column_parallel_apply() requires one state object per column.");
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
                throw std::invalid_argument("column_parallel_apply() subset overload requires one state object per selected column.");
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

        /** Lazily-built index mapping keys to row positions (mutable for const methods). */
        mutable std::unique_ptr<std::unordered_map<KeyType, size_t>> key_index;
        mutable internals::lazy_shared_ptr<internals::JsonConverter> json_converter_;

        /**
         * One sparse overlay per row. Each overlay is independently synchronized
         * so unrelated row edits do not contend with each other.
         */
        std::vector<RowOverlay> edits;

        /** Initialize an unkeyed DataFrame from a CSV reader. */
        void init_unkeyed_from_reader(CSVReader& reader) {
            this->assert_fresh_storage(false);
            this->is_keyed = false;
            this->col_names_ = reader.col_names_ptr();
            for (auto& row : reader) {
                rows.push_back(row);
                edits.emplace_back();
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
                throw std::runtime_error("Key column cannot be empty.");

            if (this->col_names_->index_of(key_column) == CSV_NOT_FOUND)
                throw std::runtime_error("Key column not found: " + key_column);

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
            this->assert_fresh_storage(true);

            for (auto& row : reader) {
                KeyType key = key_func(row);

                auto existing = key_to_pos.find(key);
                if (existing != key_to_pos.end()) {
                    if (policy == DuplicateKeyPolicy::THROW)
                        throw std::runtime_error("Duplicate key encountered.");

                    if (policy == DuplicateKeyPolicy::OVERWRITE)
                        rows[existing->second] = row;

                    continue;
                }

                rows.push_back(row);
                keys_.push_back(key);
                edits.emplace_back();
                key_to_pos[key] = rows.size() - 1;
            }
        }

        /** Find the index of a column by name. Throws if the column is not found. */
        size_t find_column(const std::string& name) const {
            return index_of(name) != CSV_NOT_FOUND ? static_cast<size_t>(index_of(name)) :
                throw std::out_of_range("Column not found: " + name);
        }

        /** Return the overlay for a specific row index. */
        const RowOverlay* find_row_edits(size_t row_index) const {
            return &edits.at(row_index);
        }

        DataFrameRow<KeyType> make_row_proxy(size_t row_index) {
            const auto& row = rows.at(row_index);
            return DataFrameRow<KeyType>(&row, this, row_index, &edits.at(row_index), this->key_ptr_at(row_index));
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
                    throw std::out_of_range("column_parallel_apply() subset overload received an invalid column index.");
                }
            }
        }

        /** Validate that this DataFrame was created with a key column. */
        void require_keyed_frame() const {
            if (!is_keyed)
                throw std::runtime_error("This DataFrame was created without a key column.");
        }

        /** Invalidate the lazy key index after structural changes. */
        void invalidate_key_index() {
            key_index.reset();
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
            return it == key_index->end() ? throw std::out_of_range("Key not found.")
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
