#pragma once
#include <type_traits>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <array>

namespace Toastbox {

// MARK: - IntState
class IntState {
public:
    // Functions provided by client
    static bool Get();
    static void Set(bool en);
    
    IntState() {
        _prev = Get();
    }
    
    IntState(bool en) {
        _prev = Get();
        Set(en);
    }
    
    IntState(const IntState& x) = delete;
    IntState(IntState&& x)      = delete;
    
    ~IntState() {
        Set(_prev);
    }
    
    void enable() {
        Set(true);
    }
    
    void disable() {
        Set(false);
    }
    
    void restore() {
        Set(_prev);
    }
    
private:
    bool _prev = false;
};

// MARK: - Scheduler

template <
    uint32_t T_UsPerTick,               // T_UsPerTick: microseconds per tick
    
    void T_Sleep(),                     // T_Sleep: sleep function; invoked when no tasks have work to do
    
    size_t T_StackGuardCount,           // T_StackGuardCount: number of pointer-sized stack guard elements to use
    void T_StackOverflow(),             // T_StackOverflow: function to call when stack overflow is detected
    auto T_StackInterrupt,              // T_StackInterrupt: interrupt stack pointer (only used to monitor
                                        //   interrupt stack for overflow; unused if T_StackGuardCount==0)
    
    typename... T_Tasks                 // T_Tasks: list of tasks
>
class Scheduler {
private:
    using _TaskFn = void(*)();
    using _RunnableFn = bool(*)();
    using _RunnableArgFn = bool(*)(void* fn, uintptr_t arg);
    
public:
    using Ticks     = unsigned int;
    using Deadline  = Ticks;
    
    // Start(): init the task's stack
    // Ints must be disabled
    template <typename T_Task>
    static void Start(_TaskFn run=T_Task::Run) {
        constexpr _Task& task = _TaskGet<T_Task>();
        void*const StackEnd = (void*)((uint8_t*)T_Task::Stack + sizeof(T_Task::Stack));
        _TaskStart(task, run, StackEnd);
    }
    
    // Stop<task>(): stops `task`
    template <typename T_Task>
    static void Stop() {
        constexpr _Task& task = _TaskGet<T_Task>();
        _TaskStop(task);
    }
    
    // Running<T_Task>(): returns whether `task` is running
    template <typename T_Task>
    static bool Running() {
        constexpr _Task& task = _TaskGet<T_Task>();
        return task.runnable!=_RunnableFalse || task.wakeDeadline;
    }
    
    // Start<Tasks>(): starts `Tasks`
    template <typename T_Task, typename T_Task2, typename... T_Tsks>
    static void Start() {
        Start<T_Task>(), Start<T_Task2>(), (Start<T_Tsks>(), ...);
    }
    
    // Stop<Tasks>(): stops `Tasks`
    template <typename T_Task, typename T_Task2, typename... T_Tsks>
    static void Stop() {
        Stop<T_Task>(), Stop<T_Task2>(), (Stop<T_Tsks>(), ...);
    }
    
    // Running<Tasks>(): returns whether any of `Tasks` are running
    template <typename T_Task, typename T_Task2, typename... T_Tsks>
    static void Running() {
        return Running<T_Task>() || Running<T_Task2>() || (Running<T_Tsks>() || ...);
    }
    
    // Running<Tasks>(): returns whether any of `Tasks` are running
    template <typename... T_Tsks>
    static void Wait() {
        return Wait([] { return !Running<T_Tsks...>(); });
    }
    
    // Run<Tasks>(): run the scheduler indefinitely
    // Automatically starts `Tasks`
    [[noreturn]]
    static void Run() {
        // Initialize each task's stack guard
        if constexpr (_StackGuardEnabled) {
            for (_Task& task : _Tasks) {
                _StackGuardInit(task.stackGuard);
            }
        }
        
        // Initialize the interrupt stack guard
        if constexpr (_InterruptStackGuardEnabled) _StackGuardInit(_InterruptStackGuard);
        
        // junk: dummy task that _TaskSwap saves the current stack pointer to,
        // which is thrown away.
        _Task junk = { .stackGuard=_Tasks[0].stackGuard, .next=&_Tasks[0] };
        _TaskCurr = &junk;
        _TaskSwap(nullptr);
        for (;;);
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        IntState ints(false);
        _TaskSwap(_RunnableTrue);
    }
    
    template <typename T>
    static T _ArgFromPtr(uintptr_t x) {
        static_assert(sizeof(T) <= sizeof(uintptr_t));
        union { T arg; uintptr_t ptr; } u = { .ptr = x };
        return u.arg;
    }

    template <typename T>
    static uintptr_t _PtrFromArg(T x) {
        static_assert(sizeof(T) <= sizeof(uintptr_t));
        union { T arg; uintptr_t ptr; } u = { .arg = x };
        return u.ptr;
    }
    
    // Wait(fn): sleep current task until `fn` returns true.
    // `fn` must not cause any task to become runnable.
    // If it does, the scheduler may not notice that the task is runnable and
    // could go to sleep instead of running the task.
    //
    // If ints are disabled before calling Wait(), ints are guaranteed to have
    // remained disabled between `fn` executing and Wait() returning, and
    // therefore the condition that `fn` checks is guaranteed to remain true
    // after Wait() returns.
    //
    // Ints are disabled while calling `fn`
    static void Wait(_RunnableFn fn) {
        IntState ints(false);
        if (fn()) return;
        _TaskSwap((_RunnableArgFn)fn);
    }
    
    template <typename T_Arg, typename T_Fn>
    static void Wait(const T_Arg& arg, T_Fn&& fn) {
        IntState ints(false);
        if (fn(arg)) return;
        _TaskSwap(std::forward<T_Fn>(fn), arg);
    }
    
    // Wait(): sleep current task until `fn` returns true, or `ticks` to pass.
    // See Wait() function above for more info.
    static bool WaitDelay(Ticks ticks, _RunnableFn fn) {
        IntState ints(false);
        if (fn()) return true;
        const Deadline deadline = _ISR.CurrentTime+ticks;
        _TaskSwap((_RunnableArgFn)fn, deadline);
        return (bool)_TaskCurr->wakeDeadline;
    }
    
    // Wait(): sleep current task until `fn` returns true, or `ticks` to pass.
    // See Wait() function above for more info.
    template <typename T_Arg, typename T_Fn>
    static bool WaitDelay(Ticks ticks, const T_Arg& arg, T_Fn&& fn) {
        IntState ints(false);
        if (fn(arg)) return true;
        const Deadline deadline = _ISR.CurrentTime+ticks;
        _TaskSwap(std::forward<T_Fn>(fn), arg, deadline);
        return (bool)_TaskCurr->wakeDeadline;
    }
    
    // WaitDeadline(): wait for a condition to become true, or for a deadline to pass.
    //
    // For a deadline to be considered in the past, it must be in the range:
    //   [CurrentTime-TicksMax/2-1, CurrentTime-1]
    // For a deadline to be considered in the future, it must be in the range:
    //   [CurrentTime, CurrentTime+TicksMax/2]
    //
    // where TicksMax is the maximum value that the `Ticks` type can hold.
    //
    // See comment in function body for more info regarding the deadline parameter.
    //
    // See Wait() function above for more info.
    static bool WaitDeadline(Deadline deadline, _RunnableFn fn) {
        IntState ints(false);
        
        // Test whether `deadline` has already passed.
        //
        // Because _ISR.CurrentTime rolls over periodically, it's impossible to differentiate
        // between `deadline` passing versus merely being far in the future. (For example,
        // consider the case where time is tracked with a uint8_t: if Deadline=127 and
        // CurrentTime=128, either Deadline passed one tick ago, or Deadline will pass
        // 255 ticks in the future.)
        //
        // To solve this ambiguity, we require deadlines to be within
        // [-TicksMax/2-1, +TicksMax/2] of _ISR.CurrentTime (where TicksMax is the maximum
        // value that the `Ticks` type can hold), which allows us to employ the following
        // heuristic:
        //
        // For a deadline to be considered in the past, it must be in the range:
        //   [CurrentTime-TicksMax/2-1, CurrentTime-1]
        // For a deadline to be considered in the future, it must be in the range:
        //   [CurrentTime, CurrentTime+TicksMax/2]
        //
        // Now that ints are disabled (and therefore _ISR.CurrentTime is unchanging), we
        // can employ the above heuristic to determine whether `deadline` has already passed.
        const bool past = deadline-_ISR.CurrentTime > _TicksMax/2;
        if (past) return false;
        if (fn()) return true;
        _TaskSwap((_RunnableArgFn)fn, deadline);
        return (bool)_TaskCurr->wakeDeadline;
    }
    
    template <typename T_Arg, typename T_Fn>
    static bool WaitDeadline(Deadline deadline, const T_Arg& arg, T_Fn&& fn) {
        IntState ints(false);
        const bool past = deadline-_ISR.CurrentTime > _TicksMax/2;
        if (past) return false;
        if (fn()) return true;
        _TaskSwap(std::forward<T_Fn>(fn), arg, deadline);
        return (bool)_TaskCurr->wakeDeadline;
    }
    
    static constexpr Ticks Us(uint16_t us) { return _TicksForUs(us); }
    static constexpr Ticks Ms(uint16_t ms) { return _TicksForUs(1000*(uint32_t)ms); }
    
    // Sleep(ticks): sleep current task for `ticks`
    static void Sleep(Ticks ticks) {
        IntState ints(false);
        const Deadline deadline = _ISR.CurrentTime+ticks;
        _TaskSwap(_RunnableFalse, deadline);
    }
    
    // Tick(): notify scheduler that a tick has passed
    // Returns whether the scheduler needs to run
    static bool Tick() {
        // Wake tasks matching the current tick.
        // We wake tasks for deadline `N` only when updating CurrentTime to
        // `N+1`, as this signifies that the full tick for `N` has elapsed.
        //
        // Put another way, we increment CurrentTime _after_ checking for
        // tasks matching `CurrentTime`.
        //
        // Put another-another way, Sleep() assigns the tasks'
        // wakeDeadline to CurrentTime+ticks (not CurrentTime+ticks+1), and
        // our logic here matches that math to ensure that we sleep at
        // least `ticks`.
        if (_ISR.WakeDeadlineUpdate || (_ISR.WakeDeadline && *_ISR.WakeDeadline==_ISR.CurrentTime)) {
            // Wake the necessary tasks, and update _ISR.WakeDeadline
            Ticks wakeDelay = _TicksMax;
            std::optional<Deadline> wakeDeadline;
            for (_Task& task : _Tasks) {
                if (!task.wakeDeadline) continue;
                if (*task.wakeDeadline == _ISR.CurrentTime) {
                    // The task's deadline has been hit; wake it
                    task.runnable = _RunnableTrue;
                    task.wakeDeadline = std::nullopt;
                
                } else {
                    // The task's deadline has not been hit; consider it as a candidate for the next _ISR.WakeDeadline
                    const Ticks d = *task.wakeDeadline-_ISR.CurrentTime;
                    if (d <= wakeDelay) {
                        wakeDelay = d;
                        wakeDeadline = task.wakeDeadline;
                    }
                }
            }
            
            _ISR.WakeDeadline = wakeDeadline;
            _ISR.WakeDeadlineUpdate = false;
        }
        
        _ISR.CurrentTime++;
        return true;
    }
    
    static Ticks CurrentTime() {
        IntState ints(false);
        return _ISR.CurrentTime;
    }
    
private:
    // MARK: - Types
    
    using _StackGuard = uintptr_t[T_StackGuardCount];
    static constexpr Ticks _TicksMax = std::numeric_limits<Ticks>::max();
    static constexpr uintptr_t _StackGuardMagicNumber = (uintptr_t)0xCAFEBABEBABECAFE;
    
    struct _Task {
        _TaskFn run = nullptr;
        _RunnableArgFn runnable = nullptr;
        void* runnableFn = nullptr;
        uintptr_t runnableArg = 0;
        std::optional<Deadline> wakeDeadline;
        void* sp = nullptr;
        _StackGuard& stackGuard;
        _Task* next = nullptr;
    };
    
    // __TaskSwap(): saves current stack pointer into spSave and restores the stack
    // pointer to spRestore.
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void __TaskSwap() {
        // __TaskSwap(): architecture-specific macro that swaps the current task
        // with a different task. Steps:
        //
        // (1) Push callee-saved regs onto stack (including $PC if needed for step #4 to work)
        // (2) Save $SP (stack pointer) into `spSave` (macro argument)
        // (3) Restore $SP (stack pointer) from `spRestore` (macro argument)
        // (4) Pop callee-saved registers from stack
        // (5) Return to caller
        #define spSave      _TaskNext->sp
        #define spRestore   _TaskCurr->sp
        
#if defined(SchedulerMSP430)
        // Architecture = MSP430
        #define _SchedulerStackAlign            1   // Count of pointer-sized registers to which the stack needs to be aligned
        #define _SchedulerStackSaveRegCount     7   // Count of pointer-sized registers that we save below
        if constexpr (sizeof(void*) == 2) {
            // Small memory model
            asm volatile("pushm #7, r10" : : : );                           // (1)
            asm volatile("mov sp, %0" : "=m" (spSave) : : );                // (2)
            asm volatile("mov %0, sp" : : "m" (spRestore) : );              // (3)
            asm volatile("popm #7, r10" : : : );                            // (4)
            asm volatile("ret" : : : );                                     // (5)
        } else {
            // Large memory model
            asm volatile("pushm.a #7, r10" : : : );                         // (1)
            asm volatile("mov.a sp, %0" : "=m" (spSave) : : );              // (2)
            asm volatile("mov.a %0, sp" : : "m" (spRestore) : );            // (3)
            asm volatile("popm.a #7, r10" : : : );                          // (4)
            asm volatile("ret.a" : : : );                                   // (5)
        }
#elif defined(SchedulerARM32)
        // Architecture = ARM32
        #define _SchedulerStackAlign            1   // Count of pointer-sized registers to which the stack needs to be aligned
        #define _SchedulerStackSaveRegCount     9   // Count of pointer-sized registers that we save below
        asm volatile("push {r4-r11,lr}" : : : );                            // (1)
        asm volatile("str sp, %0" : "=m" (spSave) : : );                    // (2)
        asm volatile("ldr sp, %0" : : "m" (spRestore) : );                  // (3)
        asm volatile("pop {r4-r11,lr}" : : : );                             // (4)
        asm volatile("bx lr" : : : );                                       // (5)
#elif defined(SchedulerAMD64)
        // Architecture = AMD64
        #define _SchedulerStackAlign            2   // Count of pointer-sized registers to which the stack needs to be aligned
        #define _SchedulerStackSaveRegCount     6   // Count of pointer-sized registers that we save below
        asm volatile("push %%rbx" : : : );                                  // (1)
        asm volatile("push %%rbp" : : : );                                  // (1)
        asm volatile("push %%r12" : : : );                                  // (1)
        asm volatile("push %%r13" : : : );                                  // (1)
        asm volatile("push %%r14" : : : );                                  // (1)
        asm volatile("push %%r15" : : : );                                  // (1)
        asm volatile("mov %%rsp, %0" : "=m" (spSave) : : );                 // (2)
        asm volatile("mov %0, %%rsp" : : "m" (spRestore) : );               // (3)
        asm volatile("pop %%r15" : : : );                                   // (4)
        asm volatile("pop %%r14" : : : );                                   // (4)
        asm volatile("pop %%r13" : : : );                                   // (4)
        asm volatile("pop %%r12" : : : );                                   // (4)
        asm volatile("pop %%rbp" : : : );                                   // (4)
        asm volatile("pop %%rbx" : : : );                                   // (4)
        asm volatile("ret" : : : );                                         // (5)
#else
        #error Task: Unspecified or unsupported architecture
#endif
        #undef spSave
        #undef spRestore
    }
    
    // MARK: - Stack Guard
    
    static void _StackGuardInit(_StackGuard& guard) {
        for (uintptr_t& x : guard) {
            x = _StackGuardMagicNumber;
        }
    }
    
    static void _StackGuardCheck(const _StackGuard& guard) {
        for (const uintptr_t& x : guard) {
            if (x != _StackGuardMagicNumber) {
                T_StackOverflow();
            }
        }
    }
    
    [[gnu::noinline]]
    static void _TaskStart(_Task& task, _TaskFn run, void* sp) {
        constexpr size_t SaveRegCount = _SchedulerStackSaveRegCount+1;
        constexpr size_t ExtraRegCount = SaveRegCount % _SchedulerStackAlign;
        constexpr size_t TotalRegCount = SaveRegCount + ExtraRegCount;
        void**const stackEnd = (void**)sp;
        // Set task run function
        task.run = run;
        // Make task runnable
        task.runnable = _RunnableTrue;
        // Reset wake deadline
        task.wakeDeadline = std::nullopt;
        // Reset stack pointer
        task.sp = stackEnd - TotalRegCount;
        // Push initial return address == _TaskRun
        *(stackEnd-ExtraRegCount-1) = (void*)_TaskRun;
    }
    
    [[gnu::noinline]]
    static void _TaskStop(_Task& task) {
        // Make task !runnable
        task.runnable = _RunnableFalse;
        // Reset wake deadline
        task.wakeDeadline = std::nullopt;
    }
    
    static void _TaskRun() {
        // Enable interrupts before entering the task for the first time
        IntState::Set(true);
        // Enter the task
        _TaskCurr->run();
        // Next task
        _TaskSwap(_RunnableFalse);
    }
    
    static _Task* _TaskNextRunnable(_Task* x) {
        for (_Task* i=x->next;; i=i->next) {
            if (i->runnable(i->runnableFn, i->runnableArg)) return i;
            if (i == x) break;
        }
        return nullptr;
    }
    
    template <typename T_Fn, typename T_Arg>
    static void _TaskSwap(T_Fn&& fn, const T_Arg& arg, std::optional<Deadline> wake=std::nullopt) {
        using Fn = bool(*)(T_Arg);
        _TaskCurr->runnableFn = (void*)+fn;
        _TaskCurr->runnableArg = _PtrFromArg(arg);
        _TaskSwap([] (void* fn, uintptr_t arg) { return ((Fn)fn)(_ArgFromPtr<T_Arg>(arg)); });
    }
    
    // _TaskSwap(): saves _TaskCurr and restores _TaskNext
    // Ints must be disabled
    [[gnu::noinline]]
    static void _TaskSwap(_RunnableArgFn fn, std::optional<Deadline> wake=std::nullopt) {
        // Check stack guards
        if constexpr (_StackGuardEnabled) _StackGuardCheck(_TaskCurr->stackGuard);
        if constexpr (_InterruptStackGuardEnabled) _StackGuardCheck(_InterruptStackGuard);
        
        // Update _TaskCurr's state
        _TaskCurr->runnable = fn;
        _TaskCurr->wakeDeadline = wake;
        _ISR.WakeDeadlineUpdate = true;
        
        // Get the next runnable task, or sleep if no task wants to run
        while (!(_TaskNext = _TaskNextRunnable(_TaskCurr))) {
            T_Sleep();
        }
        
        std::swap(_TaskCurr, _TaskNext);
        __TaskSwap();
    }
    
    static bool _RunnableTrue(void* fn, uintptr_t arg) {
        return true;
    }
    
    static bool _RunnableFalse(void* fn, uintptr_t arg) {
        return false;
    }
    
    static constexpr Ticks _TicksForUs(uint32_t us) {
        // We're intentionally not ceiling the result because Sleep() implicitly
        // ceils by adding one tick (to prevent truncated sleeps), so if this
        // function ceiled too, we'd always sleep one more tick than needed.
        return us / T_UsPerTick;
    }
    
    // _TaskGet(): returns the _Task& for the given T_Task
    template <typename T_Task, size_t T_Delta=0>
    static constexpr _Task& _TaskGet() {
        static_assert((std::is_same_v<T_Task, T_Tasks> || ...), "invalid task");
        constexpr size_t idx = (_ElmIdx<T_Task, T_Tasks...>() + T_Delta) % std::size(_Tasks);
        return _Tasks[idx];
    }
    
    template <typename T_1, typename T_2=void, typename... T_s>
    static constexpr size_t _ElmIdx() {
        return std::is_same_v<T_1,T_2> ? 0 : 1 + _ElmIdx<T_1, T_s...>();
    }
    
    static inline _Task _Tasks[sizeof...(T_Tasks)] = {
        _Task{
            .run        = nullptr,
            .runnable   = _RunnableFalse,
            .sp         = nullptr,
            .stackGuard = *(_StackGuard*)T_Tasks::Stack,
            .next       = &_TaskGet<T_Tasks, 1>(),
        }...,
    };
    
    static constexpr bool _StackGuardEnabled = (bool)T_StackGuardCount;
    static constexpr bool _InterruptStackGuardEnabled =
        _StackGuardEnabled &&
        !std::is_null_pointer<decltype(T_StackInterrupt)>::value;
    
    // _InterruptStackGuard: ideally this would be `static constexpr` instead of
    // `static inline`, but C++ doesn't allow constexpr reinterpret_cast.
    // In C++20 we could use std::bit_cast for this.
    static inline _StackGuard& _InterruptStackGuard = *(_StackGuard*)T_StackInterrupt;
    static inline _Task* _TaskCurr = nullptr;
    static inline _Task* _TaskNext = nullptr;
    
    static inline struct {
        Ticks CurrentTime = 0;
        std::optional<Deadline> WakeDeadline;
        bool WakeDeadlineUpdate = false;
    } _ISR;
};

} // namespace Toastbox
