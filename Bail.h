#pragma once
#include <iostream>
#include <cstdio>

namespace Toastbox {

// Bail() base case:
// Necessary to silence "Format string is not a string literal (potentially insecure)"
[[noreturn]] inline void Bail(const char* str) {
    std::cerr << str << std::endl;
    abort();
}

template<typename ...Args>
[[noreturn]] inline void Bail(const char* fmt, Args&&... args) {
    char msg[512];
    snprintf(msg, sizeof(msg), fmt, std::forward<Args>(args)...);
    std::cerr << msg << std::endl;
    abort();
}

} // namespace Toastbox
