#pragma once

#include <mutex>
#include "clean_data_frame.hpp"

namespace csv {
    namespace internals {
        namespace data_frame {
            /** Overlay-aware backend. Owns visible schema and all column mutation logic.
             * Physical rows stay in the stable clean backing; promotion leaves readers
             * and proxies bound to the same objects. Structural operations are exclusive.
             */
            class DirtyDataFrame : public RowBackend {
            public:
                explicit inline DirtyDataFrame(CleanDataFrame& backing, bool allocate_edits = true)
                    : backing_(&backing), names_(backing.names()) {
                    if (allocate_edits) { edits_.resize(backing.rows().size()); }
                }
                DirtyDataFrame(const DirtyDataFrame&) = delete;
                DirtyDataFrame& operator=(const DirtyDataFrame&) = delete;

                inline void rebind(CleanDataFrame& backing) noexcept { backing_ = &backing; }
                inline const ConstColNamesPtr& names() const noexcept { return names_; }
                inline size_t n_cols() const noexcept { return names_->size(); }
                inline const RowOverlay* find_edits(size_t row) const {
                    // Empty slots exist only in an unpublished, exclusive mutation
                    // backend. Published backends initialize every slot before release.
                    return edits_.empty() ? nullptr : edits_.at(row).get();
                }
                inline RowOverlay* ensure_edits(size_t row) {
                    if (auto* overlay = edits_.at(row).get()) { return overlay; }
                    std::lock_guard<std::mutex> lock(creation_lock_);
                    return edits_.at(row).ensure();
                }
                inline size_t physical_column(size_t column) const {
                    return column_indices_.empty() ? column : column_indices_.at(column);
                }
                inline size_t logical_column(size_t physical) const {
                    if (column_indices_.empty()) { return physical; }
                    const auto position = std::find(column_indices_.begin(), column_indices_.end(), physical);
                    if (position == column_indices_.end()) { throw std::runtime_error("key column is not visible"); }
                    return static_cast<size_t>(std::distance(column_indices_.begin(), position));
                }
                inline CellBinding bind(size_t row, size_t column) const {
                    validate_column(column, n_cols());
                    return CellBinding{find_edits(row), physical_column(column)};
                }
                inline csv::string_view view(size_t row, size_t column) const {
                    const CellBinding cell = bind(row, column);
                    csv::string_view value;
                    if (cell.overlay && cell.overlay->try_get_view(cell.physical_column, value)) { return value; }
                    return RowViewAccessor::view(backing_->rows().at(row), cell.physical_column);
                }
                inline CSVRow make_inserted_row(const std::vector<std::string>& row) const {
                    validate_row(row, n_cols());
                    if (column_indices_.empty() && n_cols() == backing_->n_cols()) { return RowStorageBuilder::make_row(row, backing_->names()); }
                    std::vector<std::string> expanded(backing_->n_cols());
                    for (size_t i = 0; i < row.size(); ++i) { expanded[physical_column(i)] = row[i]; }
                    return RowStorageBuilder::make_row(expanded, backing_->names());
                }
                inline void reserve_insert() {
                    backing_->reserve_insert();
                    reserve_next(edits_);
                }
                inline void insert_row(size_t index, CSVRow&& row) {
                    // Call reserve_insert before mutating keyed metadata. Slot moves
                    // and CSVRow moves cannot allocate once capacity is available.
                    backing_->insert_row(index, std::move(row));
                    edits_.insert(edits_.begin() + index, RowOverlaySlot());
                }
                inline void erase_row(size_t index) {
                    edits_.erase(edits_.begin() + index);
                    backing_->erase_row(index);
                }
                inline bool erase_column(size_t index, int key_column) {
                    if (index >= n_cols()) { return false; }
                    const size_t physical = physical_column(index);
                    if (key_column != CSV_NOT_FOUND && physical == static_cast<size_t>(key_column)) {
                        throw std::runtime_error("cannot erase key column from DataFrame");
                    }
                    std::vector<std::string> columns = names_->get_col_names();
                    columns.erase(columns.begin() + index);
                    ColNamesPtr names = std::make_shared<ColNames>();
                    names->set_policy(names_->get_policy());
                    names->set_col_names(columns);
                    std::vector<size_t> mapping = column_indices_;
                    if (mapping.empty()) {
                        mapping.reserve(backing_->n_cols());
                        for (size_t i = 0; i < backing_->n_cols(); ++i) { mapping.push_back(i); }
                    }
                    mapping.erase(mapping.begin() + index);
                    names_ = std::move(names);
                    column_indices_ = std::move(mapping);
                    return true;
                }
                inline std::unique_ptr<DirtyDataFrame> selected_rows(
                    CleanDataFrame& selected, const std::vector<std::uint8_t>& mask
                ) const {
                    std::unique_ptr<DirtyDataFrame> result(new DirtyDataFrame(selected));
                    result->names_ = names_;
                    result->column_indices_ = column_indices_;
                    size_t target = 0;
                    for (size_t i = 0; i < mask.size(); ++i) {
                        if (mask[i]) {
                            if (const auto* overlay = find_edits(i)) {
                                *result->edits_[target].ensure() = overlay->snapshot();
                            }
                            ++target;
                        }
                    }
                    return result;
                }

                template<typename BeforeReplace>
                inline void insert_column(
                    size_t index, const std::string& name, const std::string& default_value,
                    int& key_column, const BeforeReplace& before_replace
                ) {
                    if (index > n_cols()) { throw std::out_of_range("DataFrame insert_column index out of range"); }
                    if (name.empty()) { throw std::invalid_argument("inserted column name must not be empty"); }
                    if (names_->index_of(name) != CSV_NOT_FOUND) {
                        throw std::invalid_argument("inserted column name must not duplicate an existing column");
                    }
                    std::vector<std::string> columns = names_->get_col_names();
                    columns.insert(columns.begin() + index, name);
                    ColNamesPtr names = std::make_shared<ColNames>();
                    names->set_policy(names_->get_policy());
                    names->set_col_names(columns);
                    int new_key_column = key_column;
                    if (key_column != CSV_NOT_FOUND) {
                        const size_t logical = logical_column(static_cast<size_t>(key_column));
                        new_key_column = static_cast<int>(logical + (index <= logical ? 1 : 0));
                    }
                    // Select a concrete materializer once. An unpublished clean-input
                    // mutation backend has neither slot allocation nor per-field lookups.
                    std::vector<CSVRow> rebuilt = edits_.empty() && column_indices_.empty()
                        ? RowStorageBuilder::insert_column(backing_->rows().size(), names, index, default_value,
                            [this](size_t row, size_t column) { return backing_->rows()[row][column].get<csv::string_view>(); })
                        : RowStorageBuilder::insert_column(backing_->rows().size(), names, index, default_value,
                            [this](size_t row, size_t column) -> std::string {
                                const CellBinding cell = bind(row, column);
                                std::string value;
                                if (cell.overlay && cell.overlay->try_get_copy(cell.physical_column, value)) { return value; }
                                return backing_->rows()[row][cell.physical_column].get<std::string>();
                            });
                    // Prepare everything before releasing parsed backing or view-key bytes.
                    before_replace();
                    backing_->replace_rows(std::move(rebuilt), std::move(names));
                    key_column = new_key_column;
                }

            private:
                CleanDataFrame* backing_;
                ConstColNamesPtr names_;
                std::vector<RowOverlaySlot> edits_;
                std::vector<size_t> column_indices_;
                std::mutex creation_lock_;
            };
        }
    }
}
