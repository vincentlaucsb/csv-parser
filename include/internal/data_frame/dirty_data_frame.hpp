#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../csv_row.hpp"
#include "../raw_csv_data.hpp"
#include "row_overlay.hpp"

namespace csv {
    namespace internals {
        namespace data_frame {
            /** Private editing state. Clean frames need neither overlay slots nor a column map.
             *
             * The flag is published only after slots are initialized. Structural operations
             * and moves require exclusive access, just like DataFrame's row storage.
             */
            class DirtyDataFrame {
            public:
                DirtyDataFrame() = default;
                DirtyDataFrame(const DirtyDataFrame&) = delete;
                DirtyDataFrame& operator=(const DirtyDataFrame&) = delete;

                inline DirtyDataFrame(DirtyDataFrame&& other) noexcept
                    : row_count_(other.row_count_),
                      rows_begin_(other.rows_begin_),
                      edits_(std::move(other.edits_)),
                      column_indices_(std::move(other.column_indices_)),
                      active_(other.is_dirty()) {
                    other.reset(0);
                }

                inline DirtyDataFrame& operator=(DirtyDataFrame&& other) noexcept {
                    if (this != &other) {
                        this->row_count_ = other.row_count_;
                        this->rows_begin_ = other.rows_begin_;
                        this->edits_ = std::move(other.edits_);
                        this->column_indices_ = std::move(other.column_indices_);
                        this->active_.store(other.is_dirty(), std::memory_order_release);
                        other.reset(0);
                    }
                    return *this;
                }

                inline bool is_dirty() const noexcept {
                    return this->active_.load(std::memory_order_acquire);
                }

                inline void reset(size_t row_count, const CSVRow* rows_begin = nullptr) noexcept {
                    std::vector<RowOverlaySlot>().swap(this->edits_);
                    this->column_indices_.clear();
                    this->row_count_ = row_count;
                    this->rows_begin_ = rows_begin;
                    this->active_.store(false, std::memory_order_release);
                }

                inline void set_clean_row_count(size_t row_count, const CSVRow* rows_begin) noexcept {
                    CSV_DEBUG_ASSERT(!this->is_dirty());
                    this->row_count_ = row_count;
                    this->rows_begin_ = rows_begin;
                }

                inline void bind_rows(const CSVRow* rows_begin) noexcept { this->rows_begin_ = rows_begin; }

                // Cells already carry a pointer into the row vector. Deriving the
                // position only on mutation keeps their size identical to standalone cells.
                inline size_t row_index(const CSVRow* row) const noexcept {
                    return static_cast<size_t>(row - this->rows_begin_);
                }

                // Called only on the dirty read path, or under the creation lock.
                inline const RowOverlay* find_row_edits(size_t row_index) const {
                    return this->edits_.at(row_index).get();
                }

                inline RowOverlay* ensure_row_edits(size_t row_index) {
                    if (this->is_dirty()) {
                        if (auto* overlay = this->edits_.at(row_index).get()) {
                            return overlay;
                        }
                    }
                    std::lock_guard<std::mutex> lock(this->creation_lock_);
                    this->activate();
                    return this->edits_.at(row_index).ensure();
                }

                inline size_t physical_column_index(size_t logical_index) const {
                    return this->column_indices_.empty() ? logical_index : this->column_indices_.at(logical_index);
                }

                inline size_t logical_column_index(size_t physical_index) const {
                    if (this->column_indices_.empty()) {
                        return physical_index;
                    }
                    const auto position = std::find(this->column_indices_.begin(), this->column_indices_.end(), physical_index);
                    if (position == this->column_indices_.end()) {
                        throw std::runtime_error("key column is not visible");
                    }
                    return static_cast<size_t>(std::distance(this->column_indices_.begin(), position));
                }

                inline csv::string_view get_sv(const CSVRow& row, size_t row_index, size_t column_index) const {
                    const size_t physical_index = this->physical_column_index(column_index);
                    const auto* overlay = this->find_row_edits(row_index);
                    csv::string_view value;
                    if (overlay && overlay->try_get_view(physical_index, value)) {
                        return value;
                    }
                    return row[physical_index].get<csv::string_view>();
                }

                inline std::vector<std::string> expand_visible_row(
                    const std::vector<std::string>& row, size_t physical_columns
                ) const {
                    if (this->column_indices_.empty()) {
                        return row;
                    }
                    std::vector<std::string> expanded(physical_columns);
                    for (size_t i = 0; i < row.size(); ++i) {
                        expanded[this->physical_column_index(i)] = row[i];
                    }
                    return expanded;
                }

                inline void insert_row(size_t index, const CSVRow* rows_begin) {
                    if (this->is_dirty()) {
                        this->edits_.insert(this->edits_.begin() + index, RowOverlaySlot());
                    }
                    ++this->row_count_;
                    this->rows_begin_ = rows_begin;
                }

                inline void erase_row(size_t index) {
                    if (this->is_dirty()) {
                        this->edits_.erase(this->edits_.begin() + index);
                    }
                    --this->row_count_;
                }

                inline void erase_column(size_t index, size_t physical_columns) {
                    std::vector<size_t> mapping = this->column_indices_;
                    if (mapping.empty()) {
                        mapping.reserve(physical_columns);
                        for (size_t i = 0; i < physical_columns; ++i) {
                            mapping.push_back(i);
                        }
                    }
                    mapping.erase(mapping.begin() + index);
                    this->activate();
                    this->column_indices_ = std::move(mapping);
                }

                inline DirtyDataFrame selected_rows(const std::vector<std::uint8_t>& mask) const {
                    DirtyDataFrame selected;
                    for (auto include : mask) {
                        selected.row_count_ += include != 0;
                    }
                    selected.edits_.resize(selected.row_count_);
                    selected.column_indices_ = this->column_indices_;
                    size_t target = 0;
                    for (size_t i = 0; i < mask.size(); ++i) {
                        if (mask[i]) {
                            if (const auto* overlay = this->find_row_edits(i)) {
                                *selected.edits_[target].ensure() = overlay->snapshot();
                            }
                            ++target;
                        }
                    }
                    selected.active_.store(true, std::memory_order_release);
                    return selected;
                }

                inline std::vector<CSVRow> insert_column(
                    const std::vector<CSVRow>& rows, ConstColNamesPtr names,
                    size_t index, const std::string& default_value
                ) const {
                    return materialize_column_insert(rows.size(), std::move(names), index, default_value,
                        [this, &rows](size_t row_index, size_t column_index) -> std::string {
                            const size_t physical_index = this->physical_column_index(column_index);
                            const auto* overlay = this->find_row_edits(row_index);
                            std::string value;
                            if (overlay && overlay->try_get_copy(physical_index, value)) {
                                return value;
                            }
                            return rows[row_index][physical_index].get<std::string>();
                        });
                }

                /** Shared lossless storage construction; the caller supplies clean or dirty values.
                 * No CSV serialization/reparse, and no per-row backing-store allocation.
                 */
                template<typename FieldAt>
                static inline std::vector<CSVRow> materialize_column_insert(
                    size_t row_count, ConstColNamesPtr names, size_t index,
                    const std::string& default_value, FieldAt field_at
                ) {
                    std::vector<CSVRow> rebuilt;
                    rebuilt.reserve(row_count);
                    RowStorageBuilder storage(names);
                    for (size_t row = 0; row < row_count; ++row) {
                        storage.begin_row();
                        for (size_t column = 0; column < names->size(); ++column) {
                            if (column == index) {
                                storage.append_field(csv::string_view(default_value));
                            }
                            else {
                                const auto value = field_at(row, column < index ? column : column - 1);
                                storage.append_field(csv::string_view(value));
                            }
                        }
                        rebuilt.push_back(storage.finish_row());
                    }
                    return rebuilt;
                }

                static inline CSVRow make_owned_row(const std::vector<std::string>& values, ConstColNamesPtr names) {
                    RowStorageBuilder storage(std::move(names));
                    storage.begin_row();
                    for (const auto& value : values) {
                        storage.append_field(csv::string_view(value));
                    }
                    return storage.finish_row();
                }

            private:
                // Arena offsets are chunk-local. Rotate backing storage between rows to
                // retain the parser's bounded chunk sizes for large materialized tables.
                class RowStorageBuilder {
                public:
                    explicit inline RowStorageBuilder(ConstColNamesPtr names) : names_(std::move(names)) {}

                    inline void begin_row() {
                        if (!this->data_ || this->bytes_ >= CSV_CHUNK_SIZE_DEFAULT ||
                            this->data_->fields.size() >= CSV_CHUNK_SIZE_DEFAULT) {
                            this->data_ = std::make_shared<RawCSVData>();
                            this->data_->col_names = std::const_pointer_cast<ColNames>(this->names_);
                            this->bytes_ = 0;
                        }
                        this->field_start_ = this->data_->fields.size();
                        this->data_->fields.reserve_for_source_size(this->field_start_ + this->names_->size());
                    }

                    inline void append_field(csv::string_view value) {
                        const size_t offset = this->data_->quote_arena.append(value);
                        this->data_->fields.emplace_back(offset, value.size(), true);
                        this->bytes_ += value.size();
                    }

                    inline CSVRow finish_row() const {
                        return CSVRow(this->data_, 0, this->field_start_, this->data_->fields.size() - this->field_start_);
                    }

                private:
                    ConstColNamesPtr names_;
                    RawCSVDataPtr data_;
                    size_t bytes_ = 0;
                    size_t field_start_ = 0;
                };

                inline void activate() {
                    if (!this->is_dirty()) {
                        this->edits_.resize(this->row_count_);
                        this->active_.store(true, std::memory_order_release);
                    }
                }

                size_t row_count_ = 0;
                const CSVRow* rows_begin_ = nullptr;
                std::vector<RowOverlaySlot> edits_;
                std::vector<size_t> column_indices_;
                std::mutex creation_lock_;
                std::atomic<bool> active_{false};
            };
        }
    }
}
