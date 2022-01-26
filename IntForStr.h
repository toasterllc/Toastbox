#pragma once
#include <cinttypes>
#include <limits>
#include "RuntimeError.h"

namespace Toastbox {

template <typename T>
static T IntForStr(const std::string_view& s) {
    // Unsigned types
    if constexpr (!std::numeric_limits<T>::is_signed) {
        errno = 0;
        const uintmax_t i = strtoumax(s.data(), nullptr, 0);
        if (errno) throw std::system_error(errno, std::generic_category());
        
        if (i > std::numeric_limits<T>::max()) {
            throw Toastbox::RuntimeError("integer out of range: %ju", (uintmax_t)i);
        }
        
        return (T)i;
    
    // Signed types
    } else {
        errno = 0;
        const intmax_t i = strtoimax(s.data(), nullptr, 0);
        if (errno) throw std::system_error(errno, std::generic_category());
        
        if (i>std::numeric_limits<T>::max() || i<std::numeric_limits<T>::min()) {
            throw Toastbox::RuntimeError("integer out of range: %jd", (intmax_t)i);
        }
        
        return (T)i;
    }
}

template <typename T>
static void IntForStr(T& i, const std::string_view& s) {
    i = IntForStr<T>(s);
}

} // namespace Toastbox
