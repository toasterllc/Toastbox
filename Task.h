#pragma once
#include <tuple>

#define TaskBegin()                     \
    if (_jmp) goto *_jmp                \

#define _TaskYield()                    \
    do {                                \
        __label__ jmp;                  \
        _jmp = &&jmp;                   \
        return;                         \
        jmp:;                           \
    } while (0)

#define TaskYield()                     \
    do {                                \
        state = State::Wait;            \
        _TaskYield();                   \
        state = State::Run;             \
        didWork = true;                 \
    } while (0)

#define TaskWait(cond)                  \
    do {                                \
        state = State::Wait;            \
        while (!(cond)) _TaskYield();   \
        state = State::Run;             \
        didWork = true;                 \
    } while (0)

#define TaskSleepMs(ms)                 \
    do {                                \
        state = State::Wait;            \
        _sleepStartMs = TimeMs();       \
        _sleepDurationMs = (ms);        \
        do _TaskYield();                \
        while (!_sleepDone());          \
        state = State::Run;             \
        didWork = true;                 \
    } while (0)

#define TaskEnd()                       \
    do {                                \
        __label__ jmp;                  \
        state = State::Done;            \
        _jmp = &&jmp;                   \
        jmp:;                           \
        return;                         \
    } while (0)

class Task {
public:
    enum class State {
        Run,
        Wait,
        Done,
    };
    
    // Functions provided by client
    static uint32_t TimeMs();
    static void DisableInterrupts();
    static void EnableInterrupts();
    static void WaitForInterrupt();
    
    void reset() { *this = {}; }
    void run() {}
    
    State state = State::Run;
    bool didWork = false;
    
protected:
    bool _sleepDone() const {
        return (TimeMs()-_sleepStartMs) >= _sleepDurationMs;
    }
    
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
            Task::DisableInterrupts();
                // Execute every task
                bool didWork = false;
                std::apply([&](auto&... t) {
                    (_runTask(didWork, t), ...);
                }, _tasks);
                
                // If no task did anything, go to sleep
                if (!didWork) Task::WaitForInterrupt();
            Task::EnableInterrupts();
        }
    }
    
private:
    template <typename T>
    static void _runTask(bool& didWork, T& task) {
        task.didWork = false;
        switch (task.state) {
        case Task::State::Run:
        case Task::State::Wait:
            task.run();
            break;
        default:
            break;
        }
        didWork |= task.didWork;
    }
    
    std::tuple<Tasks&...> _tasks;
};
