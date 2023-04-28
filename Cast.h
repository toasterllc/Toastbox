#pragma once
#include <sstream>
#include "Util.h"

namespace Toastbox {

template<typename T_Dst, typename T_Src>
T_Dst Cast(std::shared_ptr<T_Src> src) {
    if (auto x = std::dynamic_pointer_cast<typename T_Dst::element_type>(src)) {
        return x;
    }
    abort();
}

template<typename T_Dst, typename T_Src>
T_Dst CastOrNull(std::shared_ptr<T_Src> src) {
    if (auto x = std::dynamic_pointer_cast<typename T_Dst::element_type>(src)) {
        return x;
    }
    return nullptr;
}

template<
typename T_Dst,
typename T_Src,
typename = std::enable_if_t<std::is_integral<T_Dst>::value>,
typename = std::enable_if_t<std::is_integral<T_Src>::value>
>
T_Dst Cast(const T_Src& x) {
    if (!std::in_range<T_Dst>(x)) {
        std::stringstream ss;
        ss << "can't represent value " << x << " using type with range [" <<
            std::to_string(std::numeric_limits<T_Dst>::min()) << "," <<
            std::to_string(std::numeric_limits<T_Dst>::max()) << "]";
        throw std::overflow_error(ss.str().c_str());
    }
    return static_cast<T_Dst>(x);
}

} // namespace Toastbox
