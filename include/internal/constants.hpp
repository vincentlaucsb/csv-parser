/** @file
 *  Defines CSV global constants
 */

#pragma once
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
            inline int getpagesize() {
                _SYSTEM_INFO sys_info = {};
                GetSystemInfo(&sys_info);
                return sys_info.dwPageSize;
            }

            /** Size of a memory page in bytes */
            const int PAGE_SIZE = getpagesize();

            /** Returns the amount of available mmory */
            inline unsigned long long get_available_memory()
            {
                MEMORYSTATUSEX status;
                status.dwLength = sizeof(status);
                GlobalMemoryStatusEx(&status);
                return status.ullAvailPhys;
            }
        #elif defined(__linux__) 
            // To be defined
            inline unsigned long long get_available_memory() {
                return 0;
            }

            const int PAGE_SIZE = getpagesize();
        #else
            // To be defined
            inline unsigned long long get_available_memory() {
                return 0;
            }

            const int PAGE_SIZE = 4096;
        #endif

        /** For functions that lazy load a large CSV, this determines how
         *  many bytes are read at a time
         */
        constexpr size_t ITERATION_CHUNK_SIZE = 50000000; // 50MB

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

    /** Used for counting number of rows */
    using RowCount = long long int;
}