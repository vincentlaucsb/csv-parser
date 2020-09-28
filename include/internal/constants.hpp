/** @file
 *  Defines CSV global constants
 */

#pragma once
#include <algorithm>
#include <array>
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
                return std::max(sys_info.dwPageSize, sys_info.dwAllocationGranularity);
            }

            /** Size of a memory page in bytes */
            const int PAGE_SIZE = getpagesize();
        #elif defined(__linux__) 
            const int PAGE_SIZE = getpagesize();
        #else
            const int PAGE_SIZE = 4096;
        #endif

        /** For functions that lazy load a large CSV, this determines how
         *  many bytes are read at a time
         */
        constexpr size_t ITERATION_CHUNK_SIZE = 10000000; // 10MB

        template<typename T>
        inline bool is_equal(T a, T b, T epsilon = 0.001) {
            /** Returns true if two floating point values are about the same */
            static_assert(std::is_floating_point<T>::value, "T must be a floating point type.");
            return std::abs(a - b) < epsilon;
        }

        /**  @typedef ParseFlags
         *   An enum used for describing the significance of each character
         *   with respect to CSV parsing
         */
        enum class ParseFlags {
            QUOTE       = 3, /**< Characters which may signify a quote escape */
            NOT_SPECIAL = 4, /**< Characters with no special meaning */
            DELIMITER   = 8, /**< Characters which may signify a new field */
            NEWLINE     = 12  /**< Characters which may signify a new row */
        };

        enum class CompoundParseFlags {
            QUOTE                     = 1,
            NOT_SPECIAL               = 4,
            DELIMITER                 = 8,
            NEWLINE                   = 12,
            QUOTE_ESCAPE_NOT_SPECIAL  = 0,
            QUOTE_ESCAPE_QUOTE        = 3
        };

        constexpr CompoundParseFlags compound_flag(ParseFlags flag, bool quote_escape) {
            return (CompoundParseFlags)((int)flag & (13 - (quote_escape * 10)));
        }
        
        /** Optimizations for reducing branching in parsing loop
         *
         *  Idea: The meaning of all non-quote characters changes depending
         *  on whether or not the parser is in a quote-escaped mode (0 or 1)
         */
        static_assert(compound_flag(ParseFlags::NOT_SPECIAL, false) == CompoundParseFlags::NOT_SPECIAL);
        static_assert(compound_flag(ParseFlags::QUOTE, false) == CompoundParseFlags::QUOTE);
        static_assert(compound_flag(ParseFlags::DELIMITER, false) == CompoundParseFlags::DELIMITER);
        static_assert(compound_flag(ParseFlags::NEWLINE, false) == CompoundParseFlags::NEWLINE);

        static_assert(compound_flag(ParseFlags::NOT_SPECIAL, true) == CompoundParseFlags::QUOTE_ESCAPE_NOT_SPECIAL);
        static_assert(compound_flag(ParseFlags::QUOTE, true) == CompoundParseFlags::QUOTE_ESCAPE_QUOTE);
        static_assert(compound_flag(ParseFlags::DELIMITER, true) == CompoundParseFlags::QUOTE_ESCAPE_NOT_SPECIAL);
        static_assert(compound_flag(ParseFlags::NEWLINE, true) == CompoundParseFlags::QUOTE_ESCAPE_NOT_SPECIAL);

        using ParseFlagMap = std::array<ParseFlags, 256>;
        using WhitespaceMap = std::array<bool, 256>;
    }

    /** Integer indicating a requested column wasn't found. */
    constexpr int CSV_NOT_FOUND = -1;
}