#pragma once

namespace Toastbox {

template <typename T_Dst, typename T_Src>
T_Dst Cast(std::shared_ptr<T_Src> src) {
    if (auto x = std::dynamic_pointer_cast<typename T_Dst::element_type>(src)) {
        return x;
    }
    abort();
}

template <typename T_Dst, typename T_Src>
T_Dst CastOrNull(std::shared_ptr<T_Src> src) {
    if (auto x = std::dynamic_pointer_cast<typename T_Dst::element_type>(src)) {
        return x;
    }
    return nullptr;
}

} // namespace Toastbox
