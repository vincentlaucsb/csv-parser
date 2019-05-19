#pragma once
#include "../external/string_view.hpp"

#define SUPPRESS_UNUSED_WARNING(x) (void)x

namespace csv {
    #if CMAKE_CXX_STANDARD == 17 || __cplusplus >= 201703L
        #define CSV_HAS_CXX17
    #endif

    #ifdef CSV_HAS_CXX17
        #include <string_view>
        using string_view = std::string_view;
    #else
        using string_view = nonstd::string_view;
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