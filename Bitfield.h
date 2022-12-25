#pragma once

namespace Toastbox {

template <typename T>
struct Bitfield {
    using Bit = T;
    Bitfield() {}
    Bitfield(T x) : val(x) {}
    operator T&() { return val; };
    operator const T&() const { return val; };
    T val = 0;
};

} // namespace Toastbox
