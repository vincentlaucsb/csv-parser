/** @file
 *  @brief Compact parser metadata for a single CSV field
 */

#pragma once

#include <cassert>

#include "../common.hpp"

namespace csv {
    namespace internals {
        namespace memory {
            /** A barebones class used for describing CSV fields */
            struct RawCSVField {
                RawCSVField() = default;
                RawCSVField(
                    size_t _start,
                    size_t _length,
                    bool _is_realized = false
                ) noexcept {
                    assert(_start <= CSV_CHUNK_INDEX_MAX);
                    assert(_length <= CSV_CHUNK_INDEX_MAX);
                    start = static_cast<CSVChunkIndex>(_start);
                    length = static_cast<CSVChunkIndex>(_length);
                    is_realized = _is_realized;
                }

                /** Raw row-relative start, or quote-arena logical start when is_realized is true. */
                CSVChunkIndex start = 0;

                /** Field length in the selected backing storage. */
                CSVChunkIndex length = 0;

                /** True when start/length refer to RawCSVData::quote_arena instead of RawCSVData::data. */
                bool is_realized = false;

                CONSTEXPR bool has_realized_storage() const noexcept {
                    return is_realized;
                }
            };
        }
    }
}
