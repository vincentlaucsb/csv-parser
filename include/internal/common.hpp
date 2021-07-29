/** @file
 *  A standalone header file containing shared code
 */

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <deque>

#if defined(_WIN32)
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <Windows.h>
# undef max
# undef min
#elif defined(__linux__)
# include <unistd.h>
#endif

 /** Helper macro which should be #defined as "inline"
  *  in the single header version
  */
#define CSV_INLINE

#pragma once
#include <type_traits>

#include "../external/string_view.hpp"

  // If there is another version of Hedley, then the newer one 
  // takes precedence.
  // See: https://github.com/nemequ/hedley
#include "../external/hedley.h"

namespace csv {
#ifdef _MSC_VER
#pragma region Compatibility Macros
#endif
    /**
     *  @def IF_CONSTEXPR
     *  Expands to `if constexpr` in C++17 and `if` otherwise
     *
     *  @def CONSTEXPR_VALUE
     *  Expands to `constexpr` in C++17 and `const` otherwise.
     *  Mainly used for global variables.
     *
     *  @def CONSTEXPR
     *  Expands to `constexpr` in decent compilers and `inline` otherwise.
     *  Intended for functions and methods.
     */

#define STATIC_ASSERT(x) static_assert(x, "Assertion failed")

#if CMAKE_CXX_STANDARD == 17 || __cplusplus >= 201703L
#define CSV_HAS_CXX17
#endif

#if CMAKE_CXX_STANDARD >= 14 || __cplusplus >= 	201402L
#define CSV_HAS_CXX14
#endif

#ifdef CSV_HAS_CXX17
#include <string_view>
     /** @typedef string_view
      *  The string_view class used by this library.
      */
    using string_view = std::string_view;
#else
     /** @typedef string_view
      *  The string_view class used by this library.
      */
    using string_view = nonstd::string_view;
#endif

#ifdef CSV_HAS_CXX17
    #define IF_CONSTEXPR if constexpr
    #define CONSTEXPR_VALUE constexpr

    #define CONSTEXPR_17 constexpr
#else
    #define IF_CONSTEXPR if
    #define CONSTEXPR_VALUE const

    #define CONSTEXPR_17 inline
#endif

#ifdef CSV_HAS_CXX14
    template<bool B, class T = void>
    using enable_if_t = std::enable_if_t<B, T>;

    #define CONSTEXPR_14 constexpr
    #define CONSTEXPR_VALUE_14 constexpr
#else
    template<bool B, class T = void>
    using enable_if_t = typename std::enable_if<B, T>::type;

    #define CONSTEXPR_14 inline
    #define CONSTEXPR_VALUE_14 const
#endif

    // Resolves g++ bug with regard to constexpr methods
    // See: https://stackoverflow.com/questions/36489369/constexpr-non-static-member-function-with-non-constexpr-constructor-gcc-clang-d
#if defined __GNUC__ && !defined __clang__
    #if (__GNUC__ >= 7 &&__GNUC_MINOR__ >= 2) || (__GNUC__ >= 8)
        #define CONSTEXPR constexpr
    #endif
    #else
        #ifdef CSV_HAS_CXX17
        #define CONSTEXPR constexpr
    #endif
#endif

#ifndef CONSTEXPR
#define CONSTEXPR inline
#endif

#ifdef _MSC_VER
#pragma endregion
#endif

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

        const int PAGE_SIZE = getpagesize();
#elif defined(__linux__) 
        const int PAGE_SIZE = getpagesize();
#else
        /** Size of a memory page in bytes. Used by
         *  csv::internals::CSVFieldArray when allocating blocks.
         */
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
         *
         *   @see quote_escape_flag
         */
        enum class ParseFlags {
            QUOTE_ESCAPE_QUOTE = 0, /**< A quote inside or terminating a quote_escaped field */
            QUOTE = 2 | 1,          /**< Characters which may signify a quote escape */
            NOT_SPECIAL = 4,        /**< Characters with no special meaning or escaped delimiters and newlines */
            DELIMITER = 4 | 2,      /**< Characters which signify a new field */
            NEWLINE = 4 | 2 | 1     /**< Characters which signify a new row */
        };

        /** Transform the ParseFlags given the context of whether or not the current
         *  field is quote escaped */
        constexpr ParseFlags quote_escape_flag(ParseFlags flag, bool quote_escape) noexcept {
            return (ParseFlags)((int)flag & ~((int)ParseFlags::QUOTE * quote_escape));
        }

        // Assumed to be true by parsing functions: allows for testing
        // if an item is DELIMITER or NEWLINE with a >= statement
        STATIC_ASSERT(ParseFlags::DELIMITER < ParseFlags::NEWLINE);

        /** Optimizations for reducing branching in parsing loop
         *
         *  Idea: The meaning of all non-quote characters changes depending
         *  on whether or not the parser is in a quote-escaped mode (0 or 1)
         */
        STATIC_ASSERT(quote_escape_flag(ParseFlags::NOT_SPECIAL, false) == ParseFlags::NOT_SPECIAL);
        STATIC_ASSERT(quote_escape_flag(ParseFlags::QUOTE, false) == ParseFlags::QUOTE);
        STATIC_ASSERT(quote_escape_flag(ParseFlags::DELIMITER, false) == ParseFlags::DELIMITER);
        STATIC_ASSERT(quote_escape_flag(ParseFlags::NEWLINE, false) == ParseFlags::NEWLINE);

        STATIC_ASSERT(quote_escape_flag(ParseFlags::NOT_SPECIAL, true) == ParseFlags::NOT_SPECIAL);
        STATIC_ASSERT(quote_escape_flag(ParseFlags::QUOTE, true) == ParseFlags::QUOTE_ESCAPE_QUOTE);
        STATIC_ASSERT(quote_escape_flag(ParseFlags::DELIMITER, true) == ParseFlags::NOT_SPECIAL);
        STATIC_ASSERT(quote_escape_flag(ParseFlags::NEWLINE, true) == ParseFlags::NOT_SPECIAL);

        /** An array which maps ASCII chars to a parsing flag */
        using ParseFlagMap = std::array<ParseFlags, 256>;

        /** An array which maps ASCII chars to a flag indicating if it is whitespace */
        using WhitespaceMap = std::array<bool, 256>;
    }

    /** Integer indicating a requested column wasn't found. */
    constexpr int CSV_NOT_FOUND = -1;
}
