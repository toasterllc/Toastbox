#pragma once
#include "Signal.h"
#include "Queue.h"
#include "Assert.h"

namespace Toastbox {

inline void _SignalQueueAssert(bool c) {
    Assert(c);
}

template<typename T_Item, size_t T_Count, bool T_FullReset=false, auto T_Assert=_SignalQueueAssert>
struct SignalQueue : Queue<T_Item,T_Count,T_FullReset,T_Assert> {
    T_Item pop() {
        T_Item t;
        {
            auto lock = _signal.wait([&] { return _Super::rok(); });
            t = std::move(_Super::rget());
            _Super::rpop();
        }
        return t;
    }
    
    void push(T_Item&& t) {
        {
            auto lock = _signal.wait([&] { return _Super::wok(); });
            _Super::wget() = std::move(t);
            _Super::wpush();
        }
        _signal.signalAll();
    }
    
    void stop() {
        _signal.stop();
    }
    
//    void reset() {
//        {
//            auto lock = _signal.lock();
//            _Super::reset();
//        }
//        _signal.signalAll();
//    }
    
    using _Super = Queue<T_Item,T_Count,T_FullReset,T_Assert>;
    Signal _signal;
};

} // namespace Toastbox
