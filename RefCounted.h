#pragma once
#include <optional>

namespace Toastbox {

template <typename T, auto RetainFn, auto ReleaseFn>
class RefCounted {
public:
    struct RetainType {}; static constexpr auto Retain = RetainType();
    
    // Default constructor: empty
    RefCounted() {}
    // Constructor: no retain
    RefCounted(const T& t) : _t(t) {}
    RefCounted(T&& t) : _t(std::move(t)) {}
    // Constructor: with retain
    RefCounted(RetainType, T&& t) : _t(std::move(t)) { RetainFn(*_t); }
    // Copy constructor: use copy assignment operator
    RefCounted(const RefCounted& x) { *this = x; }
    // Copy assignment operator
    RefCounted& operator=(const RefCounted& x) {
        if (x._t) RetainFn(*x._t);
        if (_t) ReleaseFn(*_t);
        _t = x._t;
        return *this;
    }
    // Move constructor: use move assignment operator
    RefCounted(RefCounted&& x) { *this = std::move(x); }
    // Move assignment operator
    RefCounted& operator=(RefCounted&& x) {
        if (_t) ReleaseFn(*_t);
        _t = std::move(x._t);
        x._t = std::nullopt;
        return *this;
    }
    
    ~RefCounted() { if (_t) ReleaseFn(*_t); }
    
    operator T() const {
        assert(_t);
        return *_t;
    }
    
    operator T() {
        assert(_t);
        return *_t;
    }
    
    bool hasValue() const { return _t.has_value(); }
    
    void reset() {
        if (_t) {
            ReleaseFn(*_t);
            _t = std::nullopt;
        }
    }
    
private:
    std::optional<T> _t;
};

} // namespace Toastbox
