#pragma once
#include <cinttypes>
#include <limits>
#include <charconv>
#include <type_traits>
#include "RuntimeError.h"

namespace Toastbox {

template<typename T>
static T IntForStr(std::string_view s, uint8_t base=10) {
    constexpr bool T_Signed = std::numeric_limits<T>::is_signed;
    using T_IntType = typename std::conditional<T_Signed, intmax_t, uintmax_t>::type;
    T_IntType i = 0;
    
    auto r = std::from_chars(s.data(), s.data()+s.size(), i, base);
    if (r.ec != std::errc()) throw RuntimeError("invalid integer: %s", std::string(s).c_str());
    
    if constexpr (T_Signed) {
        if (i>std::numeric_limits<T>::max() || i<std::numeric_limits<T>::min()) {
            throw RuntimeError("integer out of range: %jd", (intmax_t)i);
        }
    } else {
        if (i>std::numeric_limits<T>::max()) {
            throw RuntimeError("integer out of range: %ju", (intmax_t)i);
        }
    }
    
    return (T)i;
}

template <typename T>
static void IntForStr(T& i, std::string_view s, uint8_t base=10) {
    i = IntForStr<T>(s);
}

} // namespace Toastbox
