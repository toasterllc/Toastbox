#pragma once
#include <functional>

#define TaskBegin()                         \
    Task& _task = (*Task::_CurrentTask);    \
    if (_task._jmp) goto *_task._jmp;       \
    _task._setRunning();

#define TaskYield() ({                      \
    _task._setWaiting();                    \
    _TaskYield();                           \
    _task._setRunning();                    \
})

#define TaskWait(cond) ({                   \
    _task._setWaiting();                    \
    decltype(cond) c;                       \
    while (!(c = (cond))) _TaskYield();     \
    _task._setRunning();                    \
    c;                                      \
})

#define TaskSleepMs(ms) ({                  \
    _task._setSleeping(ms);                 \
    do _TaskYield();                        \
    while (!_task._sleepDone());            \
    _task._setRunning();                    \
})

#define _TaskYield() ({                     \
    __label__ jmp;                          \
    _task._jmp = &&jmp;                     \
    return;                                 \
    jmp:;                                   \
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
        Stop,
    };
    
    using TaskFn = std::function<void(void)>;
    
    template <typename T, size_t N>
    [[noreturn]] static void Run(T (&tasks)[N]) {
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
    
    template <typename ...T>
    [[noreturn]] static void Run(T&... ts) {
        std::reference_wrapper<Task> tasks[] = { static_cast<Task&>(ts)... };
        Run(tasks);
    }
    
    Task(TaskFn fn) : _fn(fn) {}
    
    void start() {
        _state = State::Run;
        _jmp = nullptr;
    }
    
    void pause() {
        _state = State::Stop;
    }
    
    void resume() {
        _state = State::Run;
    }
    
    bool run() {
        // Run the task
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
        
        switch (_state) {
        case State::Run:
            // The task terminated if it returns in the 'Run' state, so update its state
            _state = State::Stop;
            _jmp = nullptr;
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
    
    void _setWaiting() {
        _state = Task::State::Wait;
    }
    
    void _setRunning() {
        _state = Task::State::Run;
        _didWork = true;
        _IRQ.restore();
    }
    
    bool _sleepDone() const {
        return (TimeMs()-_sleepStartMs) >= _sleepDurationMs;
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
