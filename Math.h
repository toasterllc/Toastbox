#pragma once

namespace Toastbox {

template <typename T>
constexpr T DivCeil(T n, T d) {
    return (n+d-1) / d;
}

template <typename T>
constexpr T Floor(T mult, T x) {
    return (x/mult)*mult;
}

template <typename T>
constexpr T Ceil(T mult, T x) {
    return Floor(mult, x+mult-1);
}

}; // namespace Toastbox
