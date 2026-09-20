# CheckFeatures.cmake

include(CheckCXXSourceCompiles)

unset(CSV_HAS_STD_FLOATING_POINT_FROM_CHARS CACHE)

check_cxx_source_compiles("
#include <charconv>
int main()
{
    double value;
    const char* text = \"3.14\";
    return std::from_chars(text, text + 4, value).ec != std::errc();
}
" CSV_HAS_STD_FLOATING_POINT_FROM_CHARS)
