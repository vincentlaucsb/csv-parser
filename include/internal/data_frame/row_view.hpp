#pragma once

#include "../csv_row.hpp"

namespace csv {
    namespace internals {
        namespace data_frame {
            // Read the same decoded, trimmed field bytes without scalar metadata.
            struct RowViewAccessor {
                static inline csv::string_view view(const CSVRow& row, size_t column) {
                    return row.get_field_impl(column, row.data);
                }
            };
        }
    }
}
