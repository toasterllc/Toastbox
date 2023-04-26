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

// Until we can use C++23's std::to_underlying()
namespace std {

template<class Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum e) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(e);
}

} // namespace std

// C++20 std::cmp_less / std::cmp_greater / etc
namespace std {

template< class T, class U >
constexpr bool cmp_equal( T t, U u ) noexcept
{
    using UT = std::make_unsigned_t<T>;
    using UU = std::make_unsigned_t<U>;
    if constexpr (std::is_signed_v<T> == std::is_signed_v<U>)
        return t == u;
    else if constexpr (std::is_signed_v<T>)
        return t < 0 ? false : UT(t) == u;
    else
        return u < 0 ? false : t == UU(u);
}

template< class T, class U >
constexpr bool cmp_not_equal( T t, U u ) noexcept
{
    return !cmp_equal(t, u);
}

template< class T, class U >
constexpr bool cmp_less( T t, U u ) noexcept
{
    using UT = std::make_unsigned_t<T>;
    using UU = std::make_unsigned_t<U>;
    if constexpr (std::is_signed_v<T> == std::is_signed_v<U>)
        return t < u;
    else if constexpr (std::is_signed_v<T>)
        return t < 0 ? true : UT(t) < u;
    else
        return u < 0 ? false : t < UU(u);
}

template< class T, class U >
constexpr bool cmp_greater( T t, U u ) noexcept
{
    return cmp_less(u, t);
}

template< class T, class U >
constexpr bool cmp_less_equal( T t, U u ) noexcept
{
    return !cmp_greater(t, u);
}

template< class T, class U >
constexpr bool cmp_greater_equal( T t, U u ) noexcept
{
    return !cmp_less(t, u);
}

} // namespace std

// C++20 std::in_range
namespace std {

template< class R, class T >
constexpr bool in_range( T t ) noexcept
{
    return std::cmp_greater_equal(t, std::numeric_limits<R>::min()) &&
        std::cmp_less_equal(t, std::numeric_limits<R>::max());
}

} // namespace std
