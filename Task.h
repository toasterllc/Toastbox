#pragma once
#include <type_traits>
#include "Toastbox/IntState.h"

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
    
//    template <typename T_Task>
//    static void Start(TaskFn fn) {
//        _Task& task = _GetTask<T_Task>();
//        task.sp = T_Task::Stack + sizeof(T_Task::Stack);
//        task.cont = _ContStart;
//        task.start = fn;
//    }
//    
//    template <typename T_Task, TaskFn T_Fn>
//    static void Start() {
//        _Task& task = _GetTask<T_Task>();
//        task.sp = T_Task::Stack + sizeof(T_Task::Stack);
//        task.cont = _ContStart;
//        task.start = T_Fn;
//    }
    
    // Start<task,fn>(): starts `task` running with `fn`
    template <typename T_Task, typename T_Fn>
    static void Start(T_Fn&& fn) {
        constexpr _Task& task = _GetTask<T_Task>();
        task.start = fn;
        task.cont = _ContStart;
        task.sp = T_Task::Stack + sizeof(T_Task::Stack);
    }
    
    // Stop<task>(): stops `task`
    template <typename T_Task>
    static void Stop() {
        constexpr _Task& task = _GetTask<T_Task>();
        task.cont = _ContNop;
    }
    
    // Running<task>(): returns whether `task` is running
    template <typename T_Task>
    static bool Running() {
        constexpr _Task& task = _GetTask<T_Task>();
        return task.cont != _ContNop;
    }
    
    // Run(): run the tasks indefinitely
    [[noreturn]]
    static void Run() {
        for (;;) {
            do {
                IntState::SetInterruptsEnabled(false);
                
                _DidWork = false;
                for (_Task& task : _Tasks) {
                    _CurrentTask = &task;
                    task.cont();
                }
            } while (_DidWork);
            
            // Reset _Wake now that we're assured that every task has been able to observe
            // _Wake=true while interrupts were disabled during the entire process.
            _Wake = false;
            
            // No work to do
            // Go to sleep!
            IntState::WaitForInterrupt();
        }
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        _TaskPause();
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
                _TaskPause();
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
    
    static void SleepUs(uint16_t us) {
        Sleep(_TicksForUs(us));
    }
    
    static void SleepMs(uint16_t ms) {
        Sleep(_TicksForUs(1000*(uint32_t)ms));
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
            do _TaskPause();
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
#define _RegsSave()                                                                         \
         if constexpr (sizeof(void*) == 2)  asm("pushm   #7, r10" : : : );                  \
    else if constexpr (sizeof(void*) == 4)  asm("pushm.a #7, r10" : : : )

#define _RegsRestore()                                                                      \
         if constexpr (sizeof(void*) == 2)  asm("popm   #7, r10" : : : );                   \
    else if constexpr (sizeof(void*) == 4)  asm("popm.a #7, r10" : : : )

#define _PCRestore()                                                                        \
         if constexpr (sizeof(void*) == 2)  asm volatile("ret " : : : );                    \
    else if constexpr (sizeof(void*) == 4)  asm volatile("reta" : : : )

#define _SPSave(dst)                                                                        \
         if constexpr (sizeof(void*) == 2)  asm volatile("mov  r1, %0" : "=m" (dst) : : );  \
    else if constexpr (sizeof(void*) == 4)  asm volatile("mova r1, %0" : "=m" (dst) : : )

#define _SPRestore(src)                                                                     \
         if constexpr (sizeof(void*) == 2)  asm volatile("mov  %0, r1" : : "m" (src) : );   \
    else if constexpr (sizeof(void*) == 4)  asm volatile("mova %0, r1" : : "m" (src) : )
    
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
        // Future invocations should execute _ContResume
        _CurrentTask->cont = _ContResume;
        // Signal that we did work
        _TaskStartWork();
        // Invoke task function
        _CurrentTask->start();
        // The task finished
        // Future invocations should do nothing
        _CurrentTask->cont = _ContNop;
    }
    
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void _TaskPause() {
        // Save task regs
        _RegsSave();
        // Save task SP
        _SPSave(_CurrentTask->sp);
        // Disable interrupts
        // This balances enabling interrupts in _TaskStartWork(), which may or may not have been called.
        // Regardless, when returning to the scheduler, interrupts need to be disabled.
        IntState::SetInterruptsEnabled(false);
        // Restore scheduler SP
        _SPRestore(_SP);
        // Restore scheduler regs
        _RegsRestore();
        // Restore scheduler PC
        _PCRestore();
    }
    
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void _ContStart() {
        // Save scheduler regs
        _RegsSave();
        // Save scheduler SP
        _SPSave(_SP);
        // Restore task SP
        _SPRestore(_CurrentTask->sp);
        // Run task
        _TaskStart();
        // Restore scheduler SP
        _SPRestore(_SP);
        // Restore scheduler regs
        _RegsRestore();
        // Restore scheduler PC
        _PCRestore();
    }
    
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void _ContResume() {
        // Save scheduler regs
        _RegsSave();
        // Save scheduler SP
        _SPSave(_SP);
        // Restore task SP
        _SPRestore(_CurrentTask->sp);
        // Restore task regs
        _RegsRestore();
        // Restore task PC
        _PCRestore();
    }
    
    static void _ContNop() {
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
        .cont = T_Tasks::Options::AutoStart::Valid ? _ContStart : _ContNop,
        .sp = T_Tasks::Stack + sizeof(T_Tasks::Stack),
    }...};
    
    static inline bool _DidWork = false;
    static inline _Task* _CurrentTask = nullptr;
    static inline void* _SP = nullptr; // Saved stack pointer
    
    static inline Ticks _CurrentTime = 0;
    static inline bool _Wake = false;
    static inline Ticks _WakeTime = 0;
    
#undef _SPSave
#undef _SPRestore
};

} // namespace Toastbox
