#pragma once
#include <type_traits>
#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <optional>
#include "Toastbox/TaskSwap.h"

namespace Toastbox {

using TaskFn = void(*)();

struct TaskOptions {
    TaskFn AutoStart = nullptr;
    TaskFn WillStart = nullptr;
    TaskFn DidStop = nullptr;
};

template <
    uint32_t T_UsPerTick,               // T_UsPerTick: microseconds per tick
    void T_SetInterruptsEnabled(bool),  // T_SetInterruptsEnabled: function to change interrupt state
    void T_Sleep(),                     // T_Sleep: function to put processor to sleep; invoked when no tasks have work to do
    void T_Error(uint16_t),             // T_Error: function to call upon an unrecoverable error (eg stack overflow)
    auto T_MainStack,                   // T_MainStack: main stack pointer (only used to monitor main stack for overflow; unused if T_StackGuardCount==0)
    size_t T_StackGuardCount,           // T_StackGuardCount: number of pointer-sized stack guard elements to use
    typename... T_Tasks                 // T_Tasks: list of tasks
>
class Scheduler {
#define Assert(x) if (!(x)) T_Error(__LINE__)
public:
    using Ticks = unsigned int;
    
    // Start<task,fn>(): starts `task` running with `fn`
    template <typename T_Task, typename T_Fn>
    static void Start(T_Fn&& fn) {
        constexpr _Task& task = _GetTask<T_Task>();
        task.run = fn;
        task.cont = _TaskSwapInit;
        task.sp = T_Task::Stack + sizeof(T_Task::Stack);
        
        if constexpr (T_Task::Options.WillStart) {
            T_Task::Options.WillStart();
        }
    }
    
    // Stop<task>(): stops `task`
    template <typename T_Task>
    static void Stop() {
        constexpr _Task& task = _GetTask<T_Task>();
        task.cont = _TaskNop;
        
        if constexpr (T_Task::Options.DidStop) {
            T_Task::Options.DidStop();
        }
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
        // Initialize the main stack guard and each task's stack guard
        if constexpr (T_StackGuardCount) {
            _StackGuardInit(_MainStackGuard);
            for (_Task& task : _Tasks) {
                _StackGuardInit(task.stackGuard);
            }
        }
        
        for (;;) {
            do {
                _DidWork = false;
                
                for (_Task& task : _Tasks) {
                    // Disable interrupts
                    // This balances enabling interrupts in _TaskStartWork(), which may or may not have been called.
                    // Regardless, when returning to the scheduler, interrupts need to be disabled.
                    T_SetInterruptsEnabled(false);
                    
                    _CurrentTask = &task;
                    task.cont();
                    
                    // Check stack guards
                    if constexpr (T_StackGuardCount) {
                        _StackGuardCheck(_MainStackGuard);
                        _StackGuardCheck(task.stackGuard);
                    }
                }
            } while (_DidWork);
            
            // Reset _ISR.Wake now that we're assured that every task has been able to observe
            // _ISR.Wake=true while interrupts were disabled during the entire process.
            // (If interrupts were enabled, it's because we entered a task, and therefore
            // _DidWork=true. So if we get here, it's because _DidWork=false -> no tasks
            // were entered -> interrupts are still disabled.)
            _ISR.Wake = false;
            
            // No work to do
            // Go to sleep!
            T_Sleep();
            
            // Allow interrupts to fire
            T_SetInterruptsEnabled(true);
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
    
//    template <typename T_Fn>
//    static auto Wait(Ticks ticks, T_Fn&& fn) {
//        // Ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.
//        // Note that _TaskSwap() (called below) returns with ints disabled as well.
//        T_SetInterruptsEnabled(false);
//        
//        const Ticks wakeTime = _ISR.CurrentTime+ticks+1;
//        std::optional<std::invoke_result_t<T_Fn>> r;
//        do {
//            const auto fnr = fn();
//            if (fnr) {
//                r = fnr;
//                break;
//            }
//            
//            // Update _ISR.WakeTime
//            _ProposeWakeTime(wakeTime);
//            
//            // Next task
//            _TaskSwap();
//        } while (_ISR.CurrentTime != wakeTime);
//        
//        _TaskStartWork();
//        return r;
//    }
    
    template <typename T_Fn>
    static auto Wait(Ticks ticks, T_Fn&& fn) {
        // Ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.
        // Note that _TaskSwap() (called below) returns with ints disabled as well.
        T_SetInterruptsEnabled(false);
        
        const Ticks wakeTime = _ISR.CurrentTime+ticks+1;
        do {
            const auto r = fn();
            if (r) {
                _TaskStartWork();
                return std::make_optional(r);
            }
            
            // Update _ISR.WakeTime
            _ProposeWakeTime(wakeTime);
            
            // Next task
            _TaskSwap();
        } while (_ISR.CurrentTime != wakeTime);
        
        // Timeout
        _TaskStartWork();
        return std::optional<std::invoke_result_t<T_Fn>>{};
    }
    
//    template <typename T_Fn>
//    static auto Wait(Ticks ticks, T_Fn&& fn) {
//        // Ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.
//        // Note that _TaskSwap() (called below) returns with ints disabled as well.
//        T_SetInterruptsEnabled(false);
//        
//        const Ticks wakeTime = _ISR.CurrentTime+ticks+1;
//        for (;;) {
//            const auto r = fn();
//            if (r) {
//                _TaskStartWork();
//                return std::make_optional(r);
//            }
//            
//            if (_ISR.CurrentTime == wakeTime) {
//                _TaskStartWork();
//                return std::nullopt;
//            }
//            
//            _TaskSwap();
//        }
//    }
    
//    template <typename T_Fn>
//    static auto Wait(Ticks ticks, T_Fn&& fn) {
//        // Ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.
//        // Note that _TaskSwap() (called below) returns with ints disabled as well.
//        T_SetInterruptsEnabled(false);
//        const Ticks wakeTime = _ISR.CurrentTime+ticks+1;
//        std::optional<std::invoke_result_t<T_Fn>> r;
//        do {
//            // Ask function if we're done
//            const auto fnr = fn();
//            if (fnr) {
//                r = fnr;
//                break;
//            }
//        } while (_ISR.CurrentTime != wakeTime);
//        
//        _TaskStartWork();
//        return r;
//    }
    
    
//    template <typename T_Fn>
//    static auto Wait(Ticks ticks, T_Fn&& fn) {
//        // Ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.
//        // Note that _TaskSwap() (called below) returns with ints disabled as well.
//        T_SetInterruptsEnabled(false);
//        
//        const Ticks wakeTime = _ISR.CurrentTime+ticks+1;
//        do {
//            const auto r = fn();
//            if (r) {
//                _TaskStartWork();
//                return std::make_optional(r);
//            }
//            
//            // Update _ISR.WakeTime
//            const Ticks wakeDelay = wakeTime-_ISR.CurrentTime;
//            const Ticks currentWakeDelay = _ISR.WakeTime-_ISR.CurrentTime;
//            if (!currentWakeDelay || wakeDelay<currentWakeDelay) {
//                _ISR.WakeTime = wakeTime;
//            }
//            
//            // Wait until we wake because _ISR.WakeTime expired (not necessarily
//            // because of this task though)
//            do _TaskSwap();
//            while (!_ISR.Wake);
//        
//        } while (_ISR.CurrentTime != wakeTime);
//        
//        _TaskStartWork();
//        return std::nullopt;
//    }
    
    // Wait<tasks>(): sleep current task until `tasks` all stop running
    template <typename... T_Tsks>
    static void Wait() {
        Wait([] { return (!Running<T_Tsks>() && ...); });
    }
    
    static constexpr Ticks Us(uint16_t us) { return _TicksForUs(us); }
    static constexpr Ticks Ms(uint16_t ms) { return _TicksForUs(1000*(uint32_t)ms); }
    
    // Sleep(ticks): sleep current task for `ticks`
    static void Sleep(Ticks ticks) {
        // Ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.
        // Note that _TaskSwap() (called below) returns with ints disabled as well.
        T_SetInterruptsEnabled(false);
        
        const Ticks wakeTime = _ISR.CurrentTime+ticks+1;
        do {
            // Update _ISR.WakeTime
            _ProposeWakeTime(wakeTime);
            
            // Wait until we wake because _ISR.WakeTime expired (not necessarily
            // because of this task though)
            do _TaskSwap();
            while (!_ISR.Wake);
        
        } while (_ISR.CurrentTime != wakeTime);
        
        _TaskStartWork();
    }
    
    // Delay(ticks): delay current task for `ticks`, without allowing other tasks to run
    static void Delay(Ticks ticks) {
        _ISR.Delay = true;
        for (Ticks i=0;; i++) {
            T_Sleep();
            // Check break condition here so that:
            //   1. we sleep ticks+1 times, and
            //   2. ticks == ~0 works properly
            if (i == ticks) break;
        }
        _ISR.Delay = false;
    }
    
    // Tick(): notify scheduler that a tick has passed
    // Returns whether the scheduler needs to run
    static bool Tick() {
        // Don't increment time if there's an existing _ISR.Wake signal that hasn't been consumed.
        // This is necessary so that we don't miss any ticks, which could cause a task wakeup to be missed.
        if (_ISR.Wake || _ISR.Delay) return true;
        
        _ISR.CurrentTime++;
        if (_ISR.CurrentTime == _ISR.WakeTime) {
            _ISR.Wake = true;
            return true;
        }
        
        return false;
    }
    
private:
    // MARK: - Types
    
    static constexpr uintptr_t _StackGuardMagicNumber = (uintptr_t)0xCAFEBABEBABECAFE;
    using _StackGuard = uintptr_t[T_StackGuardCount];
    
    struct _Task {
        TaskFn run = nullptr;
        TaskFn cont = nullptr;
        void* sp = nullptr;
        _StackGuard& stackGuard;
    };
    
    // MARK: - Stack Guard
    
    static void _StackGuardInit(_StackGuard& guard) {
        for (uintptr_t& x : guard) {
            x = _StackGuardMagicNumber;
        }
    }
    
    static void _StackGuardCheck(const _StackGuard& guard) {
        for (const uintptr_t& x : guard) {
            Assert(x == _StackGuardMagicNumber);
        }
    }
    
    static void _TaskStartWork() {
        _DidWork = true;
        // Enable interrupts
        T_SetInterruptsEnabled(true);
    }
    
    static void _TaskStart() {
        // Future invocations should invoke _TaskSwap
        _CurrentTask->cont = _TaskSwap;
        // Signal that we did work
        _TaskStartWork();
        // Invoke task function
        _CurrentTask->run();
        // The task finished
        // Future invocations should do nothing
        _CurrentTask->cont = _TaskNop;
        // Return to scheduler
        _TaskSwap();
    }
    
    // _TaskSwapInit(): swap task in and jump to _TaskStart
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void _TaskSwapInit() {
        TaskSwap(_TaskStart, _CurrentTask->sp);
    }
    
    // _TaskSwap(): swaps the current task and the saved task
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void _TaskSwap() {
        // ## Architecture = ARM32, large memory model
        TaskSwap(nullptr, _CurrentTask->sp);
    }
    
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
    
    static void _ProposeWakeTime(Ticks wakeTime) {
        const Ticks wakeDelay = wakeTime-_ISR.CurrentTime;
        const Ticks currentWakeDelay = _ISR.WakeTime-_ISR.CurrentTime;
        if (!currentWakeDelay || wakeDelay<currentWakeDelay) {
            _ISR.WakeTime = wakeTime;
        }
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
    
#warning TODO: remove public after finished debugging
public:
    template <TaskFn T_Fn, TaskFn T_A, TaskFn T_B>
    struct _FnConditional { static constexpr TaskFn Fn = T_A; };
    template <TaskFn T_A, TaskFn T_B>
    struct _FnConditional<nullptr, T_A, T_B> { static constexpr TaskFn Fn = T_B; };
    
    static inline _Task _Tasks[] = {_Task{
        .run = T_Tasks::Options.AutoStart,
        .cont = _FnConditional<T_Tasks::Options.AutoStart, _TaskSwapInit, _TaskNop>::Fn,
        .sp = T_Tasks::Stack + sizeof(T_Tasks::Stack),
        .stackGuard = *(_StackGuard*)T_Tasks::Stack,
    }...};
    
    // _MainStackGuard: ideally this would be `static constexpr` instead of `static inline`,
    // but C++ doesn't allow constexpr reinterpret_cast.
    // In C++20 we could use std::bit_cast for this.
    static inline _StackGuard& _MainStackGuard = *(_StackGuard*)T_MainStack;
    
    static inline bool _DidWork = false;
    static inline _Task* _CurrentTask = nullptr;
    
    static volatile inline struct {
        Ticks CurrentTime = 0;
        Ticks WakeTime = 0;
        bool Wake = false;
        std::atomic<bool> Delay = false; // Atomic because we assign without disabling interrupts
    } _ISR;
#undef Assert
};

} // namespace Toastbox
