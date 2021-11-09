#pragma once

namespace Toastbox {

class IRQState {
public:
    // Functions provided by client
    static bool SetInterruptsEnabled(bool en);
    static void WaitForInterrupt();
    
    static IRQState Enabled() {
        IRQState irq;
        irq.enable();
        return irq;
    }
    
    static IRQState Disabled() {
        IRQState irq;
        irq.disable();
        return irq;
    }
    
    IRQState()                  = default;
    IRQState(const IRQState& x) = delete;
    IRQState(IRQState&& x) {
        _s = x._s;
        x._s = {};
    }
    
    ~IRQState() {
        restore();
    }
    
    void enable() {
        _Assert(!_s.prevEnValid);
        _s.prevEn = SetInterruptsEnabled(true);
        _s.prevEnValid = true;
    }
    
    void disable() {
        _Assert(!_s.prevEnValid);
        _s.prevEn = SetInterruptsEnabled(false);
        _s.prevEnValid = true;
    }
    
    void restore() {
        if (_s.prevEnValid) {
            SetInterruptsEnabled(_s.prevEn);
            _s.prevEnValid = false;
        }
    }
    
private:
    static void _Assert(bool cond) { if (!cond) abort(); }
    
    struct {
        bool prevEn = false;
        bool prevEnValid = false;
    } _s;
};

} // namespace Toastbox
