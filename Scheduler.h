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
public:
    using Ticks     = unsigned int;
    using Deadline  = Ticks;
    
    // MARK: - Types
    
    static constexpr uintptr_t _StackGuardMagicNumber = (uintptr_t)0xCAFEBABEBABECAFE;
    using _StackGuard = uintptr_t[T_StackGuardCount];
    
    using _TaskFn = void(*)();
    using _RunnableFn = bool(*)();
    
    struct _Task {
        _TaskFn run = nullptr;
        _RunnableFn runnable = nullptr;
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
    
//    // Start<task,fn>(): starts `task` running with `fn`
//    template <typename T_Task, typename T_Fn>
//    static void Start(T_Fn&& fn) {
//        constexpr _Task& task = _TaskGet<T_Task>();
//        task.run = fn;
//        task.runnable = _RunnableTrue;
//        task.sp = T_Task::Stack + sizeof(T_Task::Stack);
//    }
    
    // Start(): init the task's stack
    // Ints must be disabled
    template <typename T_Task, typename T_Fn>
    static void Start(T_Fn&& fn) {
        constexpr size_t SaveRegCount = _SchedulerStackSaveRegCount+1;
        constexpr size_t ExtraRegCount = SaveRegCount % _SchedulerStackAlign;
        constexpr size_t TotalRegCount = SaveRegCount + ExtraRegCount;
        constexpr void** StackEnd = (void**)(T_Task::Stack + sizeof(T_Task::Stack));
        constexpr _Task& task = _TaskGet<T_Task>();
        
        // Reset stack pointer
        task.sp = StackEnd - TotalRegCount;
        // Make task runnable
        task.runnable = _RunnableTrue;
        // Reset wake deadline
        task.wakeDeadline = std::nullopt;
        // Push initial return address == _TaskRun
        *(StackEnd-ExtraRegCount-1) = (void*)_TaskRun;
    }
    
    // Stop<task>(): stops `task`
    template <typename T_Task>
    static void Stop() {
        constexpr _Task& task = _TaskGet<T_Task>();
        task.runnable = _RunnableFalse;
    }
    
    // Running<task>(): returns whether `task` is running
    template <typename T_Task>
    static bool Running() {
        constexpr _Task& task = _TaskGet<T_Task>();
        return task.runnable != _RunnableFalse;
    }
    
    // Run(): run the tasks indefinitely
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
        
        
//        for (;;) {
//            do {
//                _DidWork = false;
//                
//                for (_Task& task : _Tasks) {
//                    // Disable ints before entering a task.
//                    // We're not using an IntState here because we don't want the IntState dtor
//                    // cleanup behavior when exiting our scope; we want to keep ints disabled
//                    // across tasks.
//                    IntState::Set(false);
//                    
//                    _CurrentTask = &task;
//                    task.cont();
//                    
//                    // Check stack guards
//                    if constexpr (T_StackGuardCount) {
//                        _StackGuardCheck(task.stackGuard);
//                    }
//                }
//            } while (_DidWork);
//            
//            // Reset _ISR.Wake now that we're assured that every task has been able to observe
//            // _ISR.Wake=true while ints were disabled during the entire process.
//            // (If ints were enabled, it's because we entered a task, and therefore
//            // _DidWork=true. So if we get here, it's because _DidWork=false -> no tasks
//            // were entered -> ints are still disabled.)
//            _ISR.Wake = false;
//            
//            // No work to do
//            // Go to sleep!
//            T_Sleep();
//            
//            // Allow ints to fire
//            IntState::Set(true);
//        }
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        IntState ints(false);
        // Return to scheduler
        _TaskSwap(_RunnableTrue);
        // Return to task
//        _TaskStartWork();
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
        _TaskSwap(fn);
    }
    
    // Wait(): sleep current task until `fn` returns true, or `ticks` have passed.
    // See Wait() function above for more info.
    static bool WaitDelay(Ticks ticks, _RunnableFn fn) {
        IntState ints(false);
        const Deadline deadline = _ISR.CurrentTime+ticks+1;
        _TaskSwap(fn, deadline);
        return (bool)_TaskCurr->wakeDeadline;
    }
    
    // WaitUntil(): wait for a condition to become true, or for a deadline to pass.
    //
    // For a deadline to be considered in the past, it must be in the range:
    //   [CurrentTime - TicksMax/2, CurrentTime]
    // For a deadline to be considered in the future, it must be in the range:
    //   [CurrentTime+1, CurrentTime + TicksMax/2 + 1]
    //
    // where TicksMax is the maximum value that the `Ticks` type can hold.
    //
    // See comment in function body for more info regarding the deadline parameter.
    //
    // See Wait() function above for more info.
    static bool WaitDeadline(Deadline deadline, _RunnableFn fn) {
        // IntState:
        //   - ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.CurrentTime
        //   - ints must be disabled because _WaitUntil() requires it
        //   - int state must be restored upon return because scheduler clobbers it
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
        // [-TicksMax/2, +TicksMax/2+1] of _ISR.CurrentTime (where TicksMax is the maximum
        // value that the `Ticks` type can hold), which allows us to employ the following
        // heuristic:
        //
        // For a deadline to be considered in the past, it must be in the range:
        //   [CurrentTime - TicksMax/2, CurrentTime]
        // For a deadline to be considered in the future, it must be in the range:
        //   [CurrentTime+1, CurrentTime + TicksMax/2 + 1]
        //
        // Now that ints are disabled (and therefore _ISR.CurrentTime is unchanging), we
        // can employ the above heuristic to determine whether `deadline` has already passed.
        const bool deadlinePassed = _ISR.CurrentTime-deadline <= _TicksMax/2;
        if (deadlinePassed) return false;
        _TaskSwap(fn, deadline);
        return (bool)_TaskCurr->wakeDeadline;
    }
    
    // Wait<tasks>(): sleep current task until `tasks` all stop running
    template <typename... T_Tsks>
    static void Wait() {
        Wait([] { return (!Running<T_Tsks>() && ...); });
    }
    
    static constexpr Ticks Us(uint16_t us) { return _TicksForUs(us); }
    static constexpr Ticks Ms(uint16_t ms) { return _TicksForUs(1000*(uint32_t)ms); }
    
    // Sleep(ticks): sleep current task for `ticks`
    static void Sleep(Ticks ticks) {
        IntState ints(false);
        const Deadline deadline = _ISR.CurrentTime+ticks+1;
        _TaskSwap(_RunnableFalse, deadline);
    }
    
    // TODO: revisit -- this implementation is wrong because `T_Sleep()` will return upon any interrupt, not just our tick interrupt, so a tick won't necessarily have passed
//    // Delay(ticks): delay current task for `ticks`, without allowing other tasks to run
//    static void Delay(Ticks ticks) {
//        _ISR.Delay = true;
//        for (Ticks i=0;; i++) {
//            T_Sleep();
//            // Check break condition here so that:
//            //   1. we sleep ticks+1 times, and
//            //   2. ticks == ~0 works properly
//            if (i == ticks) break;
//        }
//        _ISR.Delay = false;
//    }
    
    // Tick(): notify scheduler that a tick has passed
    // Returns whether the scheduler needs to run
    static bool Tick() {
//        // Don't increment time if there's an existing _ISR.Wake signal that hasn't been consumed.
//        // This is necessary so that we don't miss any ticks, which could cause a task wakeup to be missed.
//        if (_ISR.Wake) return true;
        
        _ISR.CurrentTime++;
        
        // Short-circuit if possible
        if (!_ISR.WakeDeadlineUpdate && (!_ISR.WakeDeadline || *_ISR.WakeDeadline!=_ISR.CurrentTime)) return false;
        
        // Wake the necessary tasks, and update _ISR.WakeDeadline
        Ticks wakeDelay = _TicksMax;
        std::optional<Deadline> wakeDeadline;
        for (_Task& task : _Tasks) {
            if (!task.wakeDeadline) continue;
            if (*task.wakeDeadline == wakeDeadline) {
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
        return true;
        
        
//        
//        
//        UpdateState
//        
//        
//        _ISR.WakeDeadline = std::nullopt;
//        for (_Task& task : _Tasks) {
//            if (task.wakeDeadline == wakeDeadline) {
//                task.runnable = _RunnableTrue;
//                task.wakeDeadline = std::nullopt;
//            }
//        }
//        
//        if (_ISR.CurrentTime == _ISR.WakeDeadline) {
//            _ISR.Wake = true;
//            return true;
//        }
        
        return false;
    }
    
    static Ticks CurrentTime() {
        // IntState:
        //   - ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.CurrentTime
        IntState ints(false);
        return _ISR.CurrentTime;
    }
    
private:
    
    static constexpr Ticks _TicksMax = std::numeric_limits<Ticks>::max();
    
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
    
//    static void _TaskStartWork() {
//        _DidWork = true;
//    }
    
//    static void _TaskRun() {
//        // Enable ints when initially entering a task.
//        // We're not using an IntState here because this function never actually returns (since
//        // we call _TaskSwap() at the end to return to the scheduler) and therefore we don't
//        // need IntState's dtor cleanup behavior.
//        IntState::Set(true);
//        // Future invocations should invoke _TaskSwap
//        _CurrentTask->cont = _TaskSwap;
//        // Signal that we did work
//        _TaskStartWork();
//        // Invoke task function
//        _CurrentTask->run();
//        // The task finished
//        // Future invocations should do nothing
//        _CurrentTask->cont = _TaskNop;
//        // Return to scheduler
//        _TaskSwap();
//    }
    
//    // _TaskStackInit(): init the task's stack
//    // Ints must be disabled
//    static void _TaskStackInit(_Task& task) {
//        const size_t extra = (_SchedulerStackSaveRegCount+1) % _SchedulerStackAlign;
//        void**& sp = *((void***)&task.sp);
//        // Reset stack pointer
//        sp = (void**)task.spInit;
//        // Push extra slots to ensure `_SchedulerStackAlign` alignment
//        sp -= extra;
//        // Push initial return address == task.run address == Task::Run
//        sp--;
//        *sp = (void*)_TaskRun;
//        // Push registers that __TaskSwap() expects to be saved on the stack.
//        // We don't care about what values the registers contain since they're not actually used.
//        sp -= _SchedulerStackSaveRegCount;
//    }
    
    [[noreturn]]
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
            if (i->runnable()) return i;
            if (i == x) break;
        }
        return nullptr;
    }
    
    // _TaskSwap(): saves _TaskCurr and restores _TaskNext
    // Ints must be disabled
    static void _TaskSwap(_RunnableFn fn, std::optional<Deadline> wake=std::nullopt) {
        // Check stack guards
        if constexpr (_StackGuardEnabled) _StackGuardCheck(_TaskCurr->stackGuard);
        if constexpr (_InterruptStackGuardEnabled) _StackGuardCheck(_InterruptStackGuard);
        
        // Update _TaskCurr's state
        _TaskCurr->runnable = fn;
        _TaskCurr->wakeDeadline = wake;
        
        // Get the next runnable task, or sleep if no task wants to run
        while (!(_TaskNext = _TaskNextRunnable(_TaskCurr))) {
            T_Sleep();
        }
        
        std::swap(_TaskCurr, _TaskNext);
        __TaskSwap();
    }
    
    static bool _RunnableTrue() {
        return true;
    }
    
    static bool _RunnableFalse() {
        return false;
    }
    
    static constexpr Ticks _TicksForUs(uint32_t us) {
        // We're intentionally not ceiling the result because Sleep() implicitly
        // ceils by adding one tick (to prevent truncated sleeps), so if this
        // function ceiled too, we'd always sleep one more tick than needed.
        return us / T_UsPerTick;
    }
    
//    // _ProposeWakeDeadline(): update _ISR.WakeDeadline with a new deadline,
//    // if it occurs before the existing deadline.
//    // Ints must be disabled
//    static void _ProposeWakeDeadline(Deadline deadline) {
//        const Ticks wakeDelay = deadline-_ISR.CurrentTime;
//        const Ticks currentWakeDelay = _ISR.WakeDeadline-_ISR.CurrentTime;
//        if (!currentWakeDelay || wakeDelay<currentWakeDelay) {
//            _ISR.WakeDeadline = deadline;
//        }
//    }
    
//    // _WaitUntil(): wait for a condition to become true, or for a deadline to pass.
//    // Ints must be disabled
//    static bool _WaitUntil(Deadline deadline, _RunnableFn fn) {
//        do {
//            // Update _ISR.WakeDeadline
//            _ProposeWakeDeadline(deadline);
//            
//            // Next task
//            _TaskSwap();
//        } while (_ISR.CurrentTime != deadline);
//        
//        // Timeout
////        _TaskStartWork();
//        return std::optional<std::invoke_result_t<T_Fn>>{};
//    }
    
    // _TaskGet(): returns the _Task& for the given T_Task
    template <typename T_Task>
    static constexpr _Task& _TaskGet() {
        static_assert((std::is_same_v<T_Task, T_Tasks> || ...), "invalid task");
        return _Tasks[_ElmIdx<T_Task, T_Tasks...>()];
    }
    
    template <typename T_1, typename T_2=void, typename... T_s>
    static constexpr size_t _ElmIdx() {
        return std::is_same_v<T_1,T_2> ? 0 : 1 + _ElmIdx<T_1, T_s...>();
    }
    
#warning TODO: remove public after finished debugging
public:
    static constexpr size_t _TaskCount = sizeof...(T_Tasks);
    
    template <size_t... T_Idx>
    static constexpr std::array<_Task,_TaskCount> _TasksGet(std::integer_sequence<size_t, T_Idx...>) {
        return {
            _Task{
                .run        = nullptr,
                .runnable   = _RunnableFalse,
                .sp         = nullptr,
                .stackGuard = *(_StackGuard*)T_Tasks::Stack,
                .next       = (T_Idx==_TaskCount-1 ? &_Tasks[0] : &_Tasks[T_Idx+1]),
            }...,
        };
    }
    
    static constexpr std::array<_Task,_TaskCount> _TasksGet() {
        return _TasksGet(std::make_integer_sequence<size_t, _TaskCount>{});
    }
    
    static inline std::array<_Task,_TaskCount> _Tasks = _TasksGet();
    
    static constexpr bool _StackGuardEnabled = (bool)T_StackGuardCount;
    static constexpr bool _InterruptStackGuardEnabled =
        _StackGuardEnabled &&
        !std::is_null_pointer<decltype(T_StackInterrupt)>::value;
    
    // _InterruptStackGuard: ideally this would be `static constexpr` instead of
    // `static inline`, but C++ doesn't allow constexpr reinterpret_cast.
    // In C++20 we could use std::bit_cast for this.
    static inline _StackGuard& _InterruptStackGuard = *(_StackGuard*)T_StackInterrupt;
    
//    static inline bool _DidWork = false;
//    static inline size_t _TaskCurrIdx = 0;
//    static inline size_t _TaskNextIdx = 0;
    
    static inline _Task* _TaskCurr = nullptr;
    static inline _Task* _TaskNext = nullptr;
    
    static inline struct {
        Ticks CurrentTime = 0;
        std::optional<Deadline> WakeDeadline;
        bool WakeDeadlineUpdate = false;
    } _ISR;
};

} // namespace Toastbox
