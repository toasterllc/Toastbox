#pragma once
#include <optional>

template <typename T, auto FreeFn>
class Uniqued {
public:
    // Default constructor: empty
    Uniqued() {}
    // Constructor
    Uniqued(const T& t) : _t(t) {}
    Uniqued(T&& t) : _t(std::move(t)) {}
    // Copy constructor: illegal
    Uniqued(const Uniqued& x) = delete;
    Uniqued& operator=(const Uniqued& x) = delete;
    // Move constructor: use move assignment operator
    Uniqued(Uniqued&& x) { *this = std::move(x); }
    // Move assignment operator
    Uniqued& operator=(Uniqued&& x) {
        if (_t) FreeFn(*_t);
        _t = std::move(x._t);
        x._t = std::nullopt;
        return *this;
    }
    
    ~Uniqued() { if (_t) FreeFn(*_t); }
    
    operator T&() {
        assert(_t);
        return *_t;
    }
    
    operator const T&() const {
        assert(_t);
        return *_t;
    }
    
//    operator bool() const { return _t.has_value(); }
    bool hasValue() const { return _t.has_value(); }
    
    void reset() {
        if (_t) {
            FreeFn(*_t);
            _t = std::nullopt;
        }
    }
    
private:
    std::optional<T> _t = {};
};
