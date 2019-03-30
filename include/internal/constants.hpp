#pragma once
#include <deque>

#include "csv_format.hpp"
#include "csv_row.hpp"

namespace csv {
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

        const int PAGE_SIZE = getpagesize();
    #elif defined(__linux__) 
        #include <unistd.h>
        const int PAGE_SIZE = getpagesize();
    #else
        const int PAGE_SIZE = 4096;
    #endif

    /** @brief Used for counting number of rows */
    using RowCount = long long int;

    using CSVCollection = std::deque<CSVRow>;

    /** @name Global Constants */
    ///@{
    /** @brief For functions that lazy load a large CSV, this determines how
    *         many bytes are read at a time
    */
    const size_t ITERATION_CHUNK_SIZE = 10000000; // 10MB

    /** @brief A dummy variable used to indicate delimiter should be guessed */
    const CSVFormat GUESS_CSV = { '\0', '"', 0, {}, false, true };

    /** @brief RFC 4180 CSV format */
    const CSVFormat DEFAULT_CSV = { ',', '"', 0, {}, false, true };

    /** @brief RFC 4180 CSV format with strict parsing */
    const CSVFormat DEFAULT_CSV_STRICT = { ',', '"', 0, {}, true, true };
    ///@}
}