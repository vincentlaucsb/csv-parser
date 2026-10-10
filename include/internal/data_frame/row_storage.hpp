#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include "../csv_exceptions.hpp"
#include "../csv_row.hpp"
#include "../raw_csv_data.hpp"
#include "row_overlay.hpp"

namespace csv {
    namespace internals {
        namespace data_frame {
            // Concrete backend identity only: deliberately no virtual functions.
            struct RowBackend {};

            struct CellBinding {
                const RowOverlay* overlay;
                size_t physical_column;
            };

            template<typename Container>
            inline void reserve_next(Container& values) {
                const size_t required = values.size() + 1;
                if (required > values.capacity()) {
                    values.reserve(std::max(required, values.capacity() * 2));
                }
            }

            inline void validate_column(size_t index, size_t count) {
                if (index >= count) { throw_column_index_out_of_range(); }
            }

            inline void validate_row(const std::vector<std::string>& row, size_t columns) {
                if (columns == 0 && !row.empty()) {
                    throw std::invalid_argument("cannot insert a non-empty row into a DataFrame without columns");
                }
                if (row.size() != columns) {
                    throw std::invalid_argument("inserted row field count must match DataFrame column count");
                }
            }

            inline std::vector<CSVRow> select_parsed_rows(
                const std::vector<CSVRow>& rows, const std::vector<std::uint8_t>& mask
            ) {
                if (mask.size() != rows.size()) {
                    throw std::invalid_argument("selected row mask size must match DataFrame row count");
                }
                std::vector<CSVRow> selected;
                selected.reserve(static_cast<size_t>(std::count_if(mask.begin(), mask.end(),
                    [](std::uint8_t value) { return value != 0; })));
                for (size_t i = 0; i < rows.size(); ++i) {
                    if (mask[i]) { selected.push_back(rows[i]); }
                }
                return selected;
            }

            /** Shared lossless owned-row construction for insertion and materialization. */
            class RowStorageBuilder {
            public:
                explicit inline RowStorageBuilder(ConstColNamesPtr names) : names_(std::move(names)) {}

                inline void begin_row() {
                    // Offsets are chunk-local. Rotate between rows instead of accumulating
                    // arbitrarily large offsets when rebuilding a large table.
                    if (!data_ || bytes_ >= CSV_CHUNK_SIZE_DEFAULT || data_->fields.size() >= CSV_CHUNK_SIZE_DEFAULT) {
                        data_ = std::make_shared<RawCSVData>();
                        data_->col_names = std::const_pointer_cast<ColNames>(names_);
                        bytes_ = 0;
                    }
                    field_start_ = data_->fields.size();
                    data_->fields.reserve_for_source_size(field_start_ + names_->size());
                }

                inline void append_field(csv::string_view value) {
                    const size_t offset = data_->quote_arena.append(value);
                    data_->fields.emplace_back(offset, value.size(), true);
                    bytes_ += value.size();
                }

                inline CSVRow finish_row() const {
                    return CSVRow(data_, 0, field_start_, data_->fields.size() - field_start_);
                }

                static inline CSVRow make_row(const std::vector<std::string>& values, ConstColNamesPtr names) {
                    RowStorageBuilder storage(std::move(names));
                    storage.begin_row();
                    for (const auto& value : values) { storage.append_field(csv::string_view(value)); }
                    return storage.finish_row();
                }

                template<typename FieldAt>
                static inline std::vector<CSVRow> insert_column(
                    size_t count, ConstColNamesPtr names, size_t index,
                    const std::string& default_value, const FieldAt& field_at
                ) {
                    std::vector<CSVRow> rebuilt;
                    rebuilt.reserve(count);
                    RowStorageBuilder storage(names);
                    for (size_t row = 0; row < count; ++row) {
                        storage.begin_row();
                        for (size_t column = 0; column < names->size(); ++column) {
                            if (column == index) { storage.append_field(csv::string_view(default_value)); }
                            else {
                                const auto value = field_at(row, column < index ? column : column - 1);
                                storage.append_field(csv::string_view(value));
                            }
                        }
                        rebuilt.push_back(storage.finish_row());
                    }
                    return rebuilt;
                }

            private:
                ConstColNamesPtr names_;
                RawCSVDataPtr data_;
                size_t bytes_ = 0;
                size_t field_start_ = 0;
            };
        }
    }
}
