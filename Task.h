#pragma once
#include <tuple>
#include <functional>

#define TaskBegin()                             \
    do {                                        \
        if (task._jmp)                          \
            goto *task._jmp;                    \
    } while (0)

#define _TaskYield()                            \
    do {                                        \
        __label__ jmp;                          \
        task._jmp = &&jmp;                      \
        return;                                 \
        jmp:;                                   \
    } while (0)

#define TaskYield()                             \
    do {                                        \
        task._state = Task::State::Wait;        \
        _TaskYield();                           \
        task._state = Task::State::Run;         \
        task._didWork = true;                   \
    } while (0)

#define TaskWait(cond)                          \
    do {                                        \
        task._state = Task::State::Wait;        \
        while (!(cond)) _TaskYield();           \
        task._state = Task::State::Run;         \
        task._didWork = true;                   \
    } while (0)

#define TaskSleepMs(ms)                         \
    do {                                        \
        task._state = Task::State::Wait;        \
        task._sleepStartMs = Task::TimeMs();    \
        task._sleepDurationMs = (ms);           \
        do _TaskYield();                        \
        while (!task._sleepDone());             \
        task._state = Task::State::Run;         \
        task._didWork = true;                   \
    } while (0)

#define TaskEnd()                               \
    do {                                        \
        __label__ jmp;                          \
        task._state = Task::State::Done;        \
        task._jmp = &&jmp;                      \
        jmp:;                                   \
        return;                                 \
    } while (0)

class IRQState {
public:
    // Functions provided by client
    static bool SetInterruptsEnabled(bool en);
    static void WaitForInterrupt();
    
    static IRQState Disabled() {
        IRQState irq;
        irq.disable();
        return irq;
    }
    
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
    enum class State {
        Run,
        Wait,
        Done,
    };
    
    using TaskFn = std::function<void(Task& task)>;
    
    // Functions provided by client
    static uint32_t TimeMs();
    
    Task(TaskFn fn) {
        _fn = fn;
    }
    
    void reset() {
        _state = State::Run;
        _jmp = nullptr;
    }
    
    void run() {
        _fn(*this);
    }
    
    bool _sleepDone() const {
        return (TimeMs()-_sleepStartMs) >= _sleepDurationMs;
    }
    
    TaskFn _fn;
    State _state = State::Run;
    bool _didWork = false;
    void* _jmp = nullptr;
    uint32_t _sleepStartMs = 0;
    uint32_t _sleepDurationMs = 0;
};

template <typename... Tasks>
class TaskScheduler {
public:
    TaskScheduler(Tasks&... tasks) : _tasks{tasks...} {}
    
    void run() {
        for (;;) {
            IRQState irq = IRQState::Disabled();
            // Execute every task
            bool didWork = false;
            std::apply([&](auto&... t) {
                (_runTask(didWork, t), ...);
            }, _tasks);
            
            // If no task performed work, go to sleep
            if (!didWork) IRQState::WaitForInterrupt();
        }
    }
    
private:
    template <typename T>
    static void _runTask(bool& didWork, T& task) {
        task._didWork = false;
        switch (task._state) {
        case Task::State::Run:
        case Task::State::Wait:
            task.run();
            break;
        default:
            break;
        }
        didWork |= task._didWork;
    }
    
    std::tuple<Tasks&...> _tasks;
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
    
    bool writeable() const {
        IRQState irq = IRQState::Disabled();
        return _writeable();
    }
    
    T read() {
        IRQState irq = IRQState::Disabled();
        if (!_readable()) return ReadResult();
        return _read();
    }
    
    void write(const T& x) {
        IRQState irq = IRQState::Disabled();
        if (!_writeable()) return;
        _write(x);
    }
    
    void reset() {
        _rptr = 0;
        _wptr = 0;
        _full = 0;
    }
    
private:
    bool _readable() const { return (_rptr!=_wptr || _full); }
    bool _writeable() const { return !_full; }
    
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
