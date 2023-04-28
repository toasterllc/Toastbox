#pragma once
#include <memory>
#include "Util.h"

namespace Toastbox {

template<typename Fn>
class DeferFn {
public:
    DeferFn(Fn&& fn) : _fn(std::move(fn)) {}
    ~DeferFn() { _fn(); }
private:
    Fn _fn;
};

template<typename F>
DeferFn<F> Defer(F&& f) {
    return DeferFn<F>(std::move(f));
};

} // namespace Toastbox

#define Defer(action) auto Concat(defer, __COUNTER__) = Toastbox::Defer([&](){ action; });
