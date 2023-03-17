#pragma once
#include "Signal.h"
#include "Queue.h"

namespace Toastbox {

template <typename T_Item, size_t T_Count, bool T_FullReset=false, auto T_Assert=nullptr>
class SignalQueue : private Queue<T_Item,T_Count,T_FullReset,T_Assert> {
public:
    auto& rget() {
        auto lock = _signal.wait([&] { return _Super::rok(); });
        return _Super::rget();
    }
    
    void rpop() {
        {
            auto lock = _signal.lock();
            _Super::rpop();
        }
        _signal.signalAll();
    }
    
    auto& wget() {
        auto lock = _signal.wait([&] { return _Super::wok(); });
        return _Super::wget();
    }
    
    void wpush() {
        {
            auto lock = _signal.lock();
            _Super::wpush();
        }
        _signal.signalAll();
    }
    
    void reset() {
        {
            auto lock = _signal.lock();
            _Super::reset();
        }
        _signal.signalAll();
    }
    
private:
    using _Super = Queue<T_Item,T_Count,T_FullReset,T_Assert>;
    Signal _signal;
};

} // namespace Toastbox
