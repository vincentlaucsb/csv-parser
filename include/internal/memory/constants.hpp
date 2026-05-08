/** @file
 *  @brief Shared limits for parser backing storage
 */

#pragma once

#include <cstdint>
#include <limits>

#include "../common.hpp"

namespace csv {
    namespace internals {
        namespace memory {
            CONSTEXPR_VALUE_14 std::uint32_t INVALID_REALIZED_OFFSET = (std::numeric_limits<std::uint32_t>::max)();
        }
    }
}
