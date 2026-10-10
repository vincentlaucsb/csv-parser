#pragma once

#include "row_storage.hpp"

namespace csv {
    namespace internals {
        namespace data_frame {
            /** Direct parsed-row backend and stable physical backing for editing.
             * First-edit promotion never moves or destroys this storage.
             */
            class CleanDataFrame : public RowBackend {
            public:
                CleanDataFrame() = default;
                CleanDataFrame(const CleanDataFrame&) = delete;
                CleanDataFrame& operator=(const CleanDataFrame&) = delete;
                CleanDataFrame(CleanDataFrame&&) noexcept = default;
                CleanDataFrame& operator=(CleanDataFrame&&) noexcept = default;

                inline const ConstColNamesPtr& names() const noexcept { return names_; }
                inline void set_names(ConstColNamesPtr names) { names_ = std::move(names); }
                inline std::vector<CSVRow>& rows() noexcept { return rows_; }
                inline const std::vector<CSVRow>& rows() const noexcept { return rows_; }
                inline size_t n_cols() const noexcept { return names_->size(); }
                inline const RowOverlay* find_edits(size_t) const noexcept { return nullptr; }

                inline CellBinding bind(size_t, size_t column) const {
                    validate_column(column, n_cols());
                    return CellBinding{nullptr, column};
                }

                inline csv::string_view view(size_t row, size_t column) const {
                    validate_column(column, n_cols());
                    return rows_.at(row)[column].get<csv::string_view>();
                }

                inline CSVRow make_inserted_row(const std::vector<std::string>& row) const {
                    validate_row(row, n_cols());
                    return RowStorageBuilder::make_row(row, names_);
                }

                inline void reserve_insert() { reserve_next(rows_); }
                inline void insert_row(size_t index, CSVRow&& row) {
                    rows_.insert(rows_.begin() + index, std::move(row));
                }
                inline void erase_row(size_t index) { rows_.erase(rows_.begin() + index); }

                inline CleanDataFrame selected_rows(const std::vector<std::uint8_t>& mask) const {
                    CleanDataFrame selected;
                    selected.names_ = names_;
                    selected.rows_ = select_parsed_rows(rows_, mask);
                    return selected;
                }

                inline void replace_rows(std::vector<CSVRow>&& rows, ConstColNamesPtr names) noexcept {
                    rows_ = std::move(rows);
                    names_ = std::move(names);
                }

            private:
                ConstColNamesPtr names_ = std::make_shared<ColNames>();
                std::vector<CSVRow> rows_;
            };
        }
    }
}
