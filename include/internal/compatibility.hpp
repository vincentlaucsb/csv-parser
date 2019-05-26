#pragma once
#include "../external/string_view.hpp"

#ifndef HEDLEY_VERSION
#include "../external/hedley.h"
#endif

#define SUPPRESS_UNUSED_WARNING(x) (void)x

namespace csv {
    #if CMAKE_CXX_STANDARD == 17 || __cplusplus >= 201703L
        #define CSV_HAS_CXX17
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
    #else
        #define IF_CONSTEXPR if
        #define CONSTEXPR_VALUE const
    #endif

    // Resolves g++ bug with regard to constexpr methods
    #ifdef __GNUC__
        #if __GNUC__ >= 7
            #if defined(CSV_HAS_CXX17) && (__GNUC_MINOR__ >= 2 || __GNUC__ >= 8)
                #define CONSTEXPR constexpr
            #endif
        #endif
    #else
        #ifdef CSV_HAS_CXX17
            #define CONSTEXPR constexpr
        #endif
    #endif

    #ifndef CONSTEXPR
        #define CONSTEXPR inline
    #endif
}