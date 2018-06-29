#pragma once
// Get operating system specific details

namespace csv {
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
}