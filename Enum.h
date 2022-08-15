#pragma once

template <typename T>
struct Enum {
    using Val = T;
    Enum() {}
    Enum(T val) : val(val) {}
    operator T&() { return val; };
    operator const T&() const { return val; };
    T val = 0;
};
