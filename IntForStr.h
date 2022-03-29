#pragma once
#include <cinttypes>
#include <limits>
#include "RuntimeError.h"

namespace Toastbox {

template <typename T>
static T IntForStr(std::string_view s, uint8_t base=0) {
    // Unsigned types
    if constexpr (!std::numeric_limits<T>::is_signed) {
        errno = 0;
        const uintmax_t i = strtoumax(s.data(), nullptr, base);
        if (errno) throw std::system_error(errno, std::generic_category());
        
        if (i > std::numeric_limits<T>::max()) {
            throw RuntimeError("integer out of range: %ju", (uintmax_t)i);
        }
        
        return (T)i;
    
    // Signed types
    } else {
        errno = 0;
        const intmax_t i = strtoimax(s.data(), nullptr, base);
        if (errno) throw std::system_error(errno, std::generic_category());
        
        if (i>std::numeric_limits<T>::max() || i<std::numeric_limits<T>::min()) {
            throw RuntimeError("integer out of range: %jd", (intmax_t)i);
        }
        
        return (T)i;
    }
}

template <typename T>
static void IntForStr(T& i, std::string_view s, uint8_t base=0) {
    i = IntForStr<T>(s);
}

} // namespace Toastbox
