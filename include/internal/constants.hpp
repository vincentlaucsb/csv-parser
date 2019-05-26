#pragma once
#include <deque>

#include "csv_format.hpp"

namespace csv {
    namespace internals {
        // Get operating system specific details
        #if defined(_WIN32)
            #include <Windows.h>
            #undef max
            #undef min

            inline int getpagesize() {
                _SYSTEM_INFO sys_info = {};
                GetSystemInfo(&sys_info);
                return sys_info.dwPageSize;
            }

            /** Size of a memory page in bytes */
            const int PAGE_SIZE = getpagesize();
        #elif defined(__linux__) 
            #include <unistd.h>
            const int PAGE_SIZE = getpagesize();
        #else
            const int PAGE_SIZE = 4096;
        #endif

        /** @brief For functions that lazy load a large CSV, this determines how
         *         many bytes are read at a time
         */
        const size_t ITERATION_CHUNK_SIZE = 50000000; // 50MB
    }

    /** @brief Used for counting number of rows */
    using RowCount = long long int;

    class CSVRow;
    using CSVCollection = std::deque<CSVRow>;
}