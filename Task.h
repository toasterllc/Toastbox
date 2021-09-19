#pragma once
#include <tuple>
#include <functional>

#define _Task (*Task::_CurrentTask)
#define _TaskIRQ (Task::_IRQ)

#define TaskBegin() ({                      \
    if (_Task._jmp) goto *_Task._jmp;       \
    _Task._setRunning();                    \
})

#define _TaskYield() ({                     \
    __label__ jmp;                          \
    _Task._jmp = &&jmp;                     \
    return;                                 \
    jmp:;                                   \
})

#define TaskYield() ({                      \
    _Task._setWaiting();                    \
    _TaskYield();                           \
    _Task._setRunning();                    \
})

#define TaskWait(cond) ({                   \
    _Task._setWaiting();                    \
    while (!(cond)) _TaskYield();           \
    _Task._setRunning();                    \
})

#define TaskRead(chan) ({                   \
    _Task._setWaiting();                    \
    TaskWait((chan).readable());            \
    _Task._setRunning();                    \
    (chan).read();                          \
})

#define TaskSleepMs(ms) ({                  \
    _Task._setSleeping(ms);                 \
    do _TaskYield();                        \
    while (!_Task._sleepDone());            \
    _Task._setRunning();                    \
})

#define TaskEnd() ({                        \
    __label__ jmp;                          \
    _Task._state = Task::State::Done;       \
    _Task._jmp = &&jmp;                     \
    jmp:;                                   \
    return;                                 \
})

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
    IRQState(IRQState&& x)      = default;
    
    ~IRQState() {
        restore();
    }
    
    void enable() {
        _Assert(!_prevEnValid);
        _prevEn = SetInterruptsEnabled(true);
        _prevEnValid = true;
    }
    
    void disable() {
        _Assert(!_prevEnValid);
        _prevEn = SetInterruptsEnabled(false);
        _prevEnValid = true;
    }
    
    void restore() {
        if (_prevEnValid) {
            SetInterruptsEnabled(_prevEn);
            _prevEnValid = false;
        }
    }
    
private:
    static void _Assert(bool cond) { if (!cond) abort(); }
    
    bool _prevEn = false;
    bool _prevEnValid = false;
};

class Task {
public:
    // Functions provided by client
    static uint32_t TimeMs();
    
    enum class State {
        Run,
        Wait,
        Done,
    };
    
    using TaskFn = std::function<void(void)>;
    
    template <typename ...Tasks>
    [[noreturn]] static void Run(Tasks&... ts) {
        const std::reference_wrapper<Task> tasks[] = { static_cast<Task&>(ts)... };
        for (;;) {
            bool didWork = false;
            do {
                _IRQ.disable();
                didWork = false;
                for (Task& task : tasks) {
                    didWork |= task.run();
                }
            } while (didWork);
            IRQState::WaitForInterrupt();
            _IRQ.restore();
        }
    }
    
    Task(TaskFn fn) : _fn(fn) {}
    
    void reset() {
        _state = State::Run;
        _jmp = nullptr;
    }
    
    bool run() {
        Task*const prevTask = _CurrentTask;
        _CurrentTask = this;
        _didWork = false;
        switch (_state) {
        case State::Run:
        case State::Wait:
            _fn();
            break;
        default:
            break;
        }
        _CurrentTask = prevTask;
        return _didWork;
    }
    
    void _setSleeping(uint32_t ms) {
        _state = Task::State::Wait;
        _sleepStartMs = Task::TimeMs();
        _sleepDurationMs = ms;
    }
    
    bool _sleepDone() const {
        return (TimeMs()-_sleepStartMs) >= _sleepDurationMs;
    }
    
    void _setWaiting() {
        _state = Task::State::Wait;
    }
    
    void _setRunning() {
        _state = Task::State::Run;
        _didWork = true;
        _IRQ.restore();
    }
    
    static inline Task* _CurrentTask = nullptr;
    static inline IRQState _IRQ;
    TaskFn _fn;
    State _state = State::Run;
    bool _didWork = false;
    void* _jmp = nullptr;
    uint32_t _sleepStartMs = 0;
    uint32_t _sleepDurationMs = 0;
};

template <typename T, size_t N>
class Channel {
public:
    class ReadResult {
    public:
        ReadResult() {}
        ReadResult(const T& x) : _x(x), _e(true) {}
        constexpr operator bool() const { return _e; }
        constexpr const T& operator*() const& { return _x; }
    
    private:
        T _x;
        bool _e = false;
    };
    
    bool readable() const {
        IRQState irq = IRQState::Disabled();
        return _readable();
    }
    
    bool writable() const {
        IRQState irq = IRQState::Disabled();
        return _writable();
    }
    
    T read() {
        for (;;) {
            IRQState irq = IRQState::Disabled();
            if (_readable()) return _read();
            IRQState::WaitForInterrupt();
        }
    }
    
    void write(const T& x) {
        for (;;) {
            IRQState irq = IRQState::Disabled();
            if (_writable()) {
                _write(x);
                return;
            }
            IRQState::WaitForInterrupt();
        }
    }
    
//    T read() {
//        IRQState irq = IRQState::Disabled();
//        _Assert(_readable());
//        return _read();
//    }
//    
//    void write(const T& x) {
//        IRQState irq = IRQState::Disabled();
//        _Assert(_writable());
//        _write(x);
//    }
    
    ReadResult readTry() {
        IRQState irq = IRQState::Disabled();
        if (!_readable()) return {};
        return _read();
    }
    
    bool writeTry(const T& x) {
        IRQState irq = IRQState::Disabled();
        if (!_writable()) return false;
        _write(x);
        return true;
    }
    
    void reset() {
        _rptr = 0;
        _wptr = 0;
        _full = 0;
    }
    
private:
    static void _Assert(bool cond) { if (!cond) abort(); }
    
    bool _readable() const  { return (_rptr!=_wptr || _full);   }
    bool _writable() const { return !_full;                    }
    
    T _read() {
        T r = _buf[_rptr];
        _rptr++;
        // Wrap _rptr to 0
        if (_rptr == N) _rptr = 0;
        _full = false;
        return r;
    }
    
    void _write(const T& x) {
        _buf[_wptr] = x;
        _wptr++;
        // Wrap _wptr to 0
        if (_wptr == N) _wptr = 0;
        // Update `_full`
        _full = (_rptr == _wptr);
    }
    
    T _buf[N];
    size_t _rptr = 0;
    size_t _wptr = 0;
    bool _full = false;
};
