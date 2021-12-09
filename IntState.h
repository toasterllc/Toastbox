#pragma once

namespace Toastbox {

class IntState {
public:
    // Functions provided by client
    static bool InterruptsEnabled();
    static void SetInterruptsEnabled(bool en);
    static void WaitForInterrupt();
    
    IntState() {
        _prevEn = InterruptsEnabled();
    }
    
    IntState(bool en) {
        _prevEn = InterruptsEnabled();
        SetInterruptsEnabled(en);
    }
    
    IntState(const IntState& x) = delete;
    IntState(IntState&& x)      = delete;
    
    ~IntState() {
        SetInterruptsEnabled(_prevEn);
    }
    
    void enable() {
        SetInterruptsEnabled(true);
    }
    
    void disable() {
        SetInterruptsEnabled(false);
    }
    
    void restore() {
        SetInterruptsEnabled(_prevEn);
    }
    
private:
    bool _prevEn = false;
};

} // namespace Toastbox
