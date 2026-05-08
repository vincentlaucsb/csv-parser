/** @file
 *  @brief Stable sidecar storage for parser-realized quoted field bytes
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "../common.hpp"
#include "block_arena.hpp"

namespace csv {
    namespace internals {
        namespace memory {
            class RawCSVQuoteArena {
            public:
                RawCSVQuoteArena() : arena_(internals::PAGE_SIZE) {}

                std::uint32_t append(csv::string_view bytes) {
                    if (bytes.empty()) {
                        return this->checked_offset(this->arena_.size());
                    }

                    auto allocation = this->arena_.allocate_contiguous(bytes.size());
                    std::copy(bytes.begin(), bytes.end(), allocation.data);
                    return allocation.offset;
                }

                RawCSVBlockArena<char>::Allocation allocate_contiguous(size_t length) {
                    return this->arena_.allocate_contiguous(length);
                }

                csv::string_view view(size_t start, size_t length) const {
                    return this->arena_.view(start, length);
                }

                void reserve_for_source_size(size_t source_size) {
                    const size_t block_capacity = (source_size + internals::PAGE_SIZE - 1) / internals::PAGE_SIZE;
                    this->arena_.reserve_blocks(block_capacity + 1);
                }

            private:
                RawCSVBlockArena<char> arena_;

                std::uint32_t checked_offset(size_t value) const noexcept {
                    assert(value <= INVALID_REALIZED_OFFSET);
                    return static_cast<std::uint32_t>(value);
                }
            };
        }
    }
}
