#pragma once

#define _Concat(x, y) x ## y
#define Concat(x, y) _Concat(x, y)

#define _Stringify(s) #s
#define Stringify(s) _Stringify(s)

#define _StaticPrint(v, x)                  \
    static constexpr auto v = (x);          \
    template<int>                           \
    struct StaticPrint;                     \
    static_assert(StaticPrint<v>::v, "")    \

#define StaticPrint(x) _StaticPrint(Concat(_StaticPrint,__COUNTER__), x)

namespace std {

// Until we can use C++23's std::to_underlying()
template<class Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum e) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(e);
}

} // namespace std
