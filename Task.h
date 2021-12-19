#pragma once
#include <type_traits>
#include "Toastbox/IntState.h"
#include "Toastbox/TaskArch.h"

namespace Toastbox {

using TaskFn = void(*)();

struct TaskOption {
    template <TaskFn T_Fn>
    struct AutoStart;
};

template <typename... T_Opts>
struct TaskOptions {
    template <typename... Args>
    struct _AutoStart {
        static constexpr bool Valid = false;
        static constexpr TaskFn Fn = nullptr;
    };
    
    template <typename T, typename... Args>
    struct _AutoStart<T, Args...> : _AutoStart<Args...> {};
    
    template <TaskFn T_Fn>
    struct _AutoStart<typename TaskOption::template AutoStart<T_Fn>> {
        static constexpr bool Valid = true;
        static constexpr TaskFn Fn = T_Fn;
    };
    
    template <TaskFn T_Fn, typename... Args>
    struct _AutoStart<typename TaskOption::template AutoStart<T_Fn>, Args...> {
        static constexpr bool Valid = true;
        static constexpr TaskFn Fn = T_Fn;
    };
    
    using AutoStart = _AutoStart<T_Opts...>;
};

template <uint32_t T_UsPerTick, typename... T_Tasks>
class Scheduler {
public:
    using Ticks = unsigned int;
    using TaskFn = void(*)();
    
    // Start<task,fn>(): starts `task` running with `fn`
    template <typename T_Task, typename T_Fn>
    static void Start(T_Fn&& fn) {
        constexpr _Task& task = _GetTask<T_Task>();
        task.start = fn;
        task.cont = _TaskSwapInit;
        task.sp = T_Task::Stack + sizeof(T_Task::Stack);
    }
    
    // Stop<task>(): stops `task`
    template <typename T_Task>
    static void Stop() {
        constexpr _Task& task = _GetTask<T_Task>();
        task.cont = _TaskNop;
    }
    
    // Running<task>(): returns whether `task` is running
    template <typename T_Task>
    static bool Running() {
        constexpr _Task& task = _GetTask<T_Task>();
        return task.cont != _TaskNop;
    }
    
    // Run(): run the tasks indefinitely
    [[noreturn]]
    static void Run() {
        for (;;) {
            do {
                _DidWork = false;
                
                for (_Task& task : _Tasks) {
                    // Disable interrupts
                    // This balances enabling interrupts in _TaskStartWork(), which may or may not have been called.
                    // Regardless, when returning to the scheduler, interrupts need to be disabled.
                    IntState::SetInterruptsEnabled(false);
                    
                    _CurrentTask = &task;
                    _SP = _CurrentTask->sp;
                    task.cont();
                    _CurrentTask->sp = _SP;
                }
            } while (_DidWork);
            
            // Reset _Wake now that we're assured that every task has been able to observe
            // _Wake=true while interrupts were disabled during the entire process.
            // (If interrupts were enabled, it's because we entered a task, and therefore
            // _DidWork=true. So if we get here, it's because _DidWork=false -> no tasks
            // were entered -> interrupts are still disabled.)
            _Wake = false;
            
            // No work to do
            // Go to sleep!
            IntState::WaitForInterrupt();
        }
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        // Return to scheduler
        _TaskSwap();
        // Return to task
        _TaskStartWork();
    }
    
    // Wait(fn): sleep current task until `fn` returns true
    // `fn` must not cause any task to become runnable.
    // If it does, the scheduler may not notice that the task is runnable and
    // could go to sleep instead of running the task.
    template <typename T_Fn>
    static auto Wait(T_Fn&& fn) {
        for (;;) {
            const auto r = fn();
            if (!r) {
                _TaskSwap();
                continue;
            }
            
            _TaskStartWork();
            return r;
        }
    }
    
    // Wait<task>(): sleep current task until `task` stops running
    template <typename T_Task>
    static void Wait() {
        Wait([] { return !Running<T_Task>(); });
    }
    
//    static void SleepUs(uint16_t us) {
//        Sleep(_TicksForUs(us));
//    }
//    
//    static void SleepMs(uint16_t ms) {
//        Sleep(_TicksForUs(1000*(uint32_t)ms));
//    }
    
    // SleepUs(us) sleep for `us` microseconds
    // Templated to ensure compile-time conversion from us->ticks
    template <uint16_t T_Us>
    static void SleepUs() {
        Sleep(_TicksForUs(T_Us));
    }
    
    // SleepMs(ms) sleep for `ms` microseconds
    // Templated to ensure compile-time conversion from ms->ticks
    template <uint16_t T_Ms>
    static void SleepMs() {
        Sleep(_TicksForUs(1000*(uint32_t)T_Ms));
    }
    
    // Sleep(ticks): sleep current task for `ticks`
    static void Sleep(Ticks ticks) {
        const Ticks wakeTime = _CurrentTime+ticks+1;
        do {
            // Update _WakeTime
            const Ticks wakeDelay = wakeTime-_CurrentTime;
            const Ticks currentWakeDelay = _WakeTime-_CurrentTime;
            if (!currentWakeDelay || wakeDelay < currentWakeDelay) {
                _WakeTime = wakeTime;
            }
            
            // Wait until some task wakes
            do _TaskSwap();
            while (!_Wake);
        
        } while (_CurrentTime != wakeTime);
        
        _TaskStartWork();
    }
    
    // Tick(): notify scheduler that a tick has passed
    // Returns whether the scheduler needs to run
    static bool Tick() {
        // Don't increment time if there's an existing _Wake signal that hasn't been consumed.
        // This is necessary so that we don't miss any ticks, which could cause a task wakeup to be missed.
        if (_Wake) return true;
        
        _CurrentTime++;
        if (_CurrentTime == _WakeTime) {
            _Wake = true;
            return true;
        }
        
        return false;
    }
    
private:
    struct _Task {
        TaskFn start = nullptr;
        TaskFn cont = nullptr;
        void* sp = nullptr;
    };
    
    static void _TaskStartWork() {
        _DidWork = true;
        // Enable interrupts
        IntState::SetInterruptsEnabled(true);
    }
    
    static void _TaskStart() {
        // Future invocations should invoke _TaskSwap
        _CurrentTask->cont = _TaskSwap;
        // Signal that we did work
        _TaskStartWork();
        // Invoke task function
        _CurrentTask->start();
        // The task finished
        // Future invocations should do nothing
        _CurrentTask->cont = _TaskNop;
    }
    
    // _TaskSwapInit(): prepare task to be swapped in, and swap it in
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void _TaskSwapInit() {
        TaskArchSwap(_SP, _SPSave, _TaskStart);
    }
    
    // _TaskSwap(): swaps the current task and the saved task
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void _TaskSwap() {
        TaskArchSwap(_SP, _SPSave, nullptr);
    }
    
//    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
//    static void _TaskInit() {
//        TaskPrepare();
//        TaskSwap();
//        
//        _TaskSwap();
//        
//        // Save scheduler regs
//        // Save scheduler SP
//        // Restore task SP
//        TaskPrologue(_SP, _CurrentTask->sp);
//        
//        // Run task
//        _TaskStart();
//        
//        // Restore scheduler SP
//        // Restore scheduler regs
//        // Restore scheduler PC
//        TaskEpilogue(_SP);
//    }
    
    static void _TaskNop() {
        // Return to scheduler
        return;
    }
    
    static constexpr Ticks _TicksForUs(uint32_t us) {
        // We're intentionally not ceiling the result because Sleep() implicitly
        // ceils by adding one tick (to prevent truncated sleeps), so if this
        // function ceiled too, we'd always sleep one more tick than needed.
        return us / T_UsPerTick;
    }
    
    // _GetTask(): returns the _Task& for the given T_Task
    template <typename T_Task>
    static constexpr _Task& _GetTask() {
        static_assert((std::is_same_v<T_Task, T_Tasks> || ...), "invalid task");
        return _Tasks[_ElmIdx<T_Task, T_Tasks...>()];
    }
    
    template <typename T_1, typename T_2=void, typename... T_s>
    static constexpr size_t _ElmIdx() {
        return std::is_same_v<T_1,T_2> ? 0 : 1 + _ElmIdx<T_1, T_s...>();
    }
    
    template <typename T_Task, typename T_Option>
    static constexpr bool _TaskHasOption() {
        return T_Task::Options::template Exists<T_Option>();
    }
    
    static inline _Task _Tasks[] = {_Task{
        .start = T_Tasks::Options::AutoStart::Fn,
        .cont = T_Tasks::Options::AutoStart::Valid ? _TaskSwapInit : _TaskNop,
        .sp = T_Tasks::Stack + sizeof(T_Tasks::Stack),
    }...};
    
    static inline bool _DidWork = false;
    static inline _Task* _CurrentTask = nullptr;
    static inline void* _SP = nullptr;
    static inline void* _SPSave = nullptr;
    
    static inline Ticks _CurrentTime = 0;
    static inline bool _Wake = false;
    static inline Ticks _WakeTime = 0;
};

} // namespace Toastbox
