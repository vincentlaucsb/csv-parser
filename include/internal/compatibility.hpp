#pragma once
#include "../external/string_view.hpp"

#define SUPPRESS_UNUSED_WARNING(x) (void)x

namespace csv {
    #if __cplusplus >= 201703L
        #include <string_view>
        using string_view = std::string_view;
    #else
        using string_view = nonstd::string_view;
    #endif

    // Resolves g++ bug with regard to constexpr methods
    #ifdef __GNUC__
        #if __GNUC__ >= 7 && __GNUC_MINOR__ >= 2
            #if __cplusplus >= 201703L
                #define CONSTEXPR constexpr
            #else
                #define CONSTEXPR
            #endif
        #endif
    #else
        #if __cplusplus >= 201703L
            #define CONSTEXPR constexpr
        #else
            #define CONSTEXPR
        #endif
    #endif
}