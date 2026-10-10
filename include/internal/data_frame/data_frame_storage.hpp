#pragma once

#include <atomic>
#include "dirty_data_frame.hpp"

namespace csv {
    namespace internals {
        namespace data_frame {
            /** C++11 tagged choice of concrete backends, without a vtable.
             *
             * The clean backing remains alive after first-edit publication. A reader
             * that loaded the old pointer can safely finish without a per-read lock.
             * Only structural operations (including moves) retire a dirty backend;
             * those require exclusive access and invalidate proxies.
             */
            class DataFrameStorage {
            public:
                inline DataFrameStorage() : active_(&clean_) {}
                DataFrameStorage(const DataFrameStorage&) = delete;
                DataFrameStorage& operator=(const DataFrameStorage&) = delete;

                inline DataFrameStorage(DataFrameStorage&& other) noexcept
                    : clean_(std::move(other.clean_)), dirty_(std::move(other.dirty_)), active_(&clean_) {
                    restore_choice();
                    other.active_.store(&other.clean_, std::memory_order_release);
                }
                inline DataFrameStorage& operator=(DataFrameStorage&& other) noexcept {
                    if (this != &other) {
                        clean_ = std::move(other.clean_);
                        dirty_ = std::move(other.dirty_);
                        restore_choice();
                        other.active_.store(&other.clean_, std::memory_order_release);
                    }
                    return *this;
                }

                // Both concrete backends use the same native row vector.
                inline std::vector<CSVRow>& rows() noexcept { return clean_.rows(); }
                inline const std::vector<CSVRow>& rows() const noexcept { return clean_.rows(); }
                inline void set_names(ConstColNamesPtr names) { clean_.set_names(std::move(names)); }
                inline const ConstColNamesPtr& names() const noexcept { return dispatch(Names()); }
                inline size_t n_cols() const noexcept { return dispatch(Count()); }
                inline CellBinding bind(size_t row, size_t column) const { return dispatch(Bind{row, column}); }
                inline csv::string_view view(size_t row, size_t column) const { return dispatch(View{row, column}); }
                inline const RowOverlay* find_edits(const CSVRow* row) const {
                    return dispatch(Edits{row_index(row)});
                }
                inline RowOverlay* ensure_edits(const CSVRow* row) {
                    return promote().ensure_edits(row_index(row));
                }
                inline CSVRow make_inserted_row(const std::vector<std::string>& row) const {
                    return dispatch(MakeRow{row});
                }
                inline void reserve_insert() { dispatch_mutable(Reserve()); }
                inline void insert_row(size_t index, CSVRow&& row) { dispatch_mutable(Insert{index, row}); }
                inline void erase_row(size_t index) { dispatch_mutable(Erase{index}); }

                inline bool erase_column(size_t index, int key_column) {
                    if (index >= n_cols()) { return false; }
                    return promote().erase_column(index, key_column);
                }

                template<typename BeforeReplace>
                inline void insert_column(
                    size_t index, const std::string& name, const std::string& value,
                    int& key_column, const BeforeReplace& before_replace
                ) {
                    RowBackend* choice = active_.load(std::memory_order_acquire);
                    if (choice == &clean_) {
                        // Structural mutation is exclusive: this transient dirty backend
                        // needs no overlay slots and never becomes visible to cell readers.
                        DirtyDataFrame mutation(clean_, false);
                        mutation.insert_column(index, name, value, key_column, before_replace);
                    }
                    else {
                        static_cast<DirtyDataFrame*>(choice)->insert_column(index, name, value, key_column, before_replace);
                        active_.store(&clean_, std::memory_order_release);
                        dirty_.reset();
                    }
                }

                inline DataFrameStorage selected_rows(const std::vector<std::uint8_t>& mask) const {
                    DataFrameStorage selected;
                    selected.clean_ = clean_.selected_rows(mask);
                    const RowBackend* choice = active_.load(std::memory_order_acquire);
                    if (choice != &clean_) {
                        selected.dirty_ = static_cast<const DirtyDataFrame*>(choice)->selected_rows(selected.clean_, mask);
                        selected.active_.store(selected.dirty_.get(), std::memory_order_release);
                    }
                    return selected;
                }

                inline bool pristine() const noexcept {
                    return active_.load(std::memory_order_acquire) == &clean_ && rows().empty();
                }

            private:
                template<typename Operation>
                inline auto dispatch(const Operation& operation) const
                    -> decltype(operation(std::declval<const CleanDataFrame&>())) {
                    const RowBackend* choice = active_.load(std::memory_order_acquire);
                    if (choice == &clean_) { return operation(clean_); }
                    return operation(*static_cast<const DirtyDataFrame*>(choice));
                }
                template<typename Operation>
                inline auto dispatch_mutable(const Operation& operation)
                    -> decltype(operation(std::declval<CleanDataFrame&>())) {
                    RowBackend* choice = active_.load(std::memory_order_acquire);
                    if (choice == &clean_) { return operation(clean_); }
                    return operation(*static_cast<DirtyDataFrame*>(choice));
                }

                inline DirtyDataFrame& promote() {
                    RowBackend* choice = active_.load(std::memory_order_acquire);
                    if (choice != &clean_) { return *static_cast<DirtyDataFrame*>(choice); }
                    std::lock_guard<std::mutex> lock(transition_lock_);
                    choice = active_.load(std::memory_order_acquire);
                    if (choice == &clean_) {
                        // Construct the schema and complete slot array before release.
                        dirty_.reset(new DirtyDataFrame(clean_));
                        choice = dirty_.get();
                        active_.store(choice, std::memory_order_release);
                    }
                    return *static_cast<DirtyDataFrame*>(choice);
                }
                inline size_t row_index(const CSVRow* row) const noexcept {
                    return static_cast<size_t>(row - rows().data());
                }
                inline void restore_choice() noexcept {
                    if (dirty_) {
                        dirty_->rebind(clean_);
                        active_.store(dirty_.get(), std::memory_order_release);
                    }
                    else { active_.store(&clean_, std::memory_order_release); }
                }

                struct Names { template<typename B> const ConstColNamesPtr& operator()(const B& b) const { return b.names(); } };
                struct Count { template<typename B> size_t operator()(const B& b) const { return b.n_cols(); } };
                struct Bind {
                    size_t row, column;
                    template<typename B> CellBinding operator()(const B& b) const { return b.bind(row, column); }
                };
                struct View {
                    size_t row, column;
                    template<typename B> csv::string_view operator()(const B& b) const { return b.view(row, column); }
                };
                struct Edits {
                    size_t row;
                    template<typename B> const RowOverlay* operator()(const B& b) const { return b.find_edits(row); }
                };
                struct MakeRow {
                    const std::vector<std::string>& row;
                    template<typename B> CSVRow operator()(const B& b) const { return b.make_inserted_row(row); }
                };
                struct Reserve { template<typename B> void operator()(B& b) const { b.reserve_insert(); } };
                struct Insert {
                    size_t index; CSVRow& row;
                    template<typename B> void operator()(B& b) const { b.insert_row(index, std::move(row)); }
                };
                struct Erase {
                    size_t index;
                    template<typename B> void operator()(B& b) const { b.erase_row(index); }
                };

                CleanDataFrame clean_;
                std::unique_ptr<DirtyDataFrame> dirty_;
                std::mutex transition_lock_;
                std::atomic<RowBackend*> active_;
            };
        }
    }
}
