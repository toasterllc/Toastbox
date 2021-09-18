#pragma once
#include <tuple>
#include <functional>

#define TaskBegin()                             \
    if (task._jmp) goto *task._jmp              \

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
    static void DisableInterrupts();
    static void EnableInterrupts();
    static void WaitForInterrupt();
    
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
