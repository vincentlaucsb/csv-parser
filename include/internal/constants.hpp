/** @file
 *  Defines CSV global constants
 */

#pragma once
#include <algorithm>
#include <cstdlib>
#include <deque>

#if defined(_WIN32)
#include <Windows.h>
#define WIN32_LEAN_AND_MEAN
#undef max
#undef min
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace csv {
    namespace internals {
        // PAGE_SIZE macro could be already defined by the host system.
        #if defined(PAGE_SIZE)
        #undef PAGE_SIZE
        #endif

        // Get operating system specific details
        #if defined(_WIN32)
            inline size_t getpagesize() {
                _SYSTEM_INFO sys_info = {};
                GetSystemInfo(&sys_info);
                return std::max(sys_info.dwPageSize, sys_info.dwAllocationGranularity);
            }

            /** Size of a memory page in bytes */
            const size_t PAGE_SIZE = getpagesize();
        #elif defined(__linux__) 
            const size_t PAGE_SIZE = getpagesize();
        #else
            const size_t PAGE_SIZE = 4096;
        #endif

        /** For functions that lazy load a large CSV, this determines how
         *  many bytes are read at a time
         */
        const size_t ITERATION_CHUNK_SIZE = std::min((size_t)10000000, PAGE_SIZE); // 10MB

        // TODO: Move to another header file
        template<typename T>
        inline bool is_equal(T a, T b, T epsilon = 0.001) {
            /** Returns true if two floating point values are about the same */
            static_assert(std::is_floating_point<T>::value, "T must be a floating point type.");
            return std::abs(a - b) < epsilon;
        }
    }

    /** Integer indicating a requested column wasn't found. */
    constexpr int CSV_NOT_FOUND = -1;
}