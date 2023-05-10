#pragma once
#include <type_traits>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <array>
#include <limits>
#include <ratio>

// SchedulerStack: macro to apply appropriate attributes to stack declarations
// gnu::used is apparently necessary for the gnu::section attribute to work when
// link-time optimization is enabled.
#define SchedulerStack(sect) [[gnu::section(sect), gnu::used]] alignas(void*)

namespace Toastbox {

// MARK: - IntState
class IntState {
public:
    // Functions provided by client
    static bool Get();
    static void Set(bool en);
    
    [[gnu::always_inline]]
    IntState() {
        _prev = Get();
    }
    
    [[gnu::always_inline]]
    IntState(bool en) {
        _prev = Get();
        Set(en);
    }
    
    IntState(const IntState& x) = delete;
    IntState(IntState&& x)      = delete;
    
    [[gnu::always_inline]]
    ~IntState() {
        Set(_prev);
    }
    
    [[gnu::always_inline]]
    void enable() {
        Set(true);
    }
    
    [[gnu::always_inline]]
    void disable() {
        Set(false);
    }
    
    [[gnu::always_inline]]
    void restore() {
        Set(_prev);
    }
    
private:
    bool _prev = false;
};

// MARK: - Scheduler
template<
typename T_TicksPeriod,             // T_TicksPeriod: a std::ratio specifying the period between Tick()
                                    //   calls, in seconds

void T_Sleep(),                     // T_Sleep: sleep function; invoked when no tasks have work to do.
                                    //   T_Sleep() is called with interrupts disabled, and interrupts must
                                    //   be disabled upon return. Implementations may temporarily enable
                                    //   interrupts if required for the CPU to wake from sleep, as long as
                                    //   interrupts are disabled upon return from T_Sleep().

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

private:
    using _TaskFn = void(*)();
    using _RunnableFn = bool(*)();
    
    // _Ticks(): returns the ceiled number of ticks required for T_Time to pass
    template<auto T_Time, typename T_Unit>
    static constexpr Ticks _Ticks() {
        using TicksPerUnitTime = std::ratio_divide<T_Unit, T_TicksPeriod>;
        using TicksRatio = std::ratio_multiply<std::ratio<T_Time>, TicksPerUnitTime>;
        const auto ticks = (TicksRatio::num + TicksRatio::den - 1) / TicksRatio::den;
        static_assert(ticks <= _TicksMax);
        return ticks;
    }
    
public:
    using TicksPeriod = T_TicksPeriod;
    
    // Run(): scheduler entry point
    // Invokes task 0's Run() function
    [[noreturn]]
    static void Run() {
        _StackGuardInit();
        _TaskRun();
        for (;;);
    }
    
    template<typename T_Task>
    static constexpr void* _StackEnd() {
        return (void*)((uint8_t*)T_Task::Stack + sizeof(T_Task::Stack));
    }
    
    // Current(): returns whether any of T_Tasks are the currently-running task
    template<typename... T_Task>
    static bool Current() {
        return ((((_TaskCurr == &_TaskGet<T_Task>()) || ...)));
    }
    
    // Start(): start running T_Task with a specified function
    template<typename T_Task>
    static void Start(_TaskFn run) {
        _TaskStart(_TaskGet<T_Task>(), run, _StackEnd<T_Task>());
    }
    
    // Start(): start running T_Tasks with their respective Run() functions
    template<typename... T_Task>
    static void Start() {
        ((_TaskStart(_TaskGet<T_Task>(), T_Task::Run, _StackEnd<T_Task>()), ...));
    }
    
    // Stop(): stop T_Tasks
    // This explicitly does not affect the current task; see Abort() for that behavior.
    template<typename... T_Task>
    static void Stop() {
        ((_TaskStop(_TaskGet<T_Task>()), ...));
    }
    
    // Abort(): the same as Stop(), except if one of `T_Tasks` is the current task, immediately returns the scheduler
    template<typename... T_Task>
    static void Abort() {
        ((_TaskStop(_TaskGet<T_Task>()), ...));
        if (Current<T_Task...>()) {
            // We stopped the current task so return to the scheduler
            _TaskSwap(_RunnableFalse);
        }
    }
    
    // Running(): returns whether any T_Tasks are running
    template<typename... T_Task>
    static bool Running() {
        return (((_TaskGet<T_Task>().runnable!=_RunnableFalse || _TaskGet<T_Task>().wakeDeadline) || ...));
    }
    
    // Wait(): waits until none of T_Tasks are running
    template<typename... T_Task>
    static void Wait() {
        return Wait([] { return !Running<T_Tsks...>(); });
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        IntState ints(false);
        _TaskSwap(_RunnableTrue);
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
    
    // Wait(): sleep current task until `fn` returns true, or `ticks` to pass.
    // See Wait() function above for more info.
    static bool Wait(Ticks ticks, _RunnableFn fn) {
        IntState ints(false);
        if (fn()) return true;
        _TaskSwap(fn, _DeadlineForTicks(ticks));
        return (bool)_TaskCurr->wakeDeadline;
    }
    
    // Context getter for current task
    template<typename T>
    static T Ctx() { return _TFromPtr<T>(_TaskCurr->ctx); }
    
    // Context setter for current task
    template<typename T>
    static void Ctx(const T& t) { _TaskCurr->ctx = _PtrFromT(t); }
    
    // WaitDeadline(): wait for a condition to become true, or for a deadline to pass.
    //
    // For a deadline to be considered in the past, it must be in the range:
    //   [CurrentTime-TicksMax/2, CurrentTime]
    // For a deadline to be considered in the future, it must be in the range:
    //   [CurrentTime+1, CurrentTime+TicksMax/2+1]
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
        //   [CurrentTime-TicksMax/2, CurrentTime]
        // For a deadline to be considered in the future, it must be in the range:
        //   [CurrentTime+1, CurrentTime+TicksMax/2+1]
        //
        // Now that ints are disabled (and therefore _ISR.CurrentTime is unchanging), we
        // can employ the above heuristic to determine whether `deadline` has already passed.
        const bool past = deadline-_ISR.CurrentTime-1 > _TicksMax/2;
        if (past) return false;
        if (fn()) return true;
        _TaskSwap(fn, deadline);
        return (bool)_TaskCurr->wakeDeadline;
    }
    
    template<auto T>
    static constexpr Ticks Us = _Ticks<T, std::micro>();
    
    template<auto T>
    static constexpr Ticks Ms = _Ticks<T, std::milli>();
    
    // Sleep(ticks): sleep current task for `ticks`
    static void Sleep(Ticks ticks) {
        IntState ints(false);
        _TaskSwap(_RunnableFalse, _DeadlineForTicks(ticks));
    }
    
    // Delay(ticks): delay current task for `ticks` without allowing other tasks to run
    // Enables interrupts at least once.
    static void Delay(Ticks ticks) {
        IntState ints(false);
        _TaskCurr->wakeDeadline = _DeadlineForTicks(ticks);
        _ISR.WakeDeadlineUpdate = true;
        
        do {
            T_Sleep();
            // Let interrupts fire after waking
            IntState ints(true);
        } while (_TaskCurr->wakeDeadline);
    }
    
    // Tick(): notify scheduler that a tick has passed
    // Returns whether the CPU should wake to allow the scheduler to run
    static bool Tick() {
        _ISR.CurrentTime++;
        
        // Wake tasks matching the current tick.
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
        return true;
    }
    
    // TickRequired(): whether Tick() invocations are currently required, as determined by whether there
    // are any tasks waiting for a deadline to pass.
    // If TickRequired() returns false, Tick() invocations aren't currently needed (and therefore
    // related hardware can be paused to save power, for example).
    static bool TickRequired() {
        IntState ints(false);
        return _ISR.WakeDeadline || _ISR.WakeDeadlineUpdate;
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
        _RunnableFn runnable = nullptr;
        std::optional<Deadline> wakeDeadline;
        void* sp = nullptr;
        uintptr_t ctx = 0;
        _StackGuard* stackGuard = nullptr;
        _Task* next = nullptr;
    };
    
    // _DeadlineForTicks: returns the deadline for `ticks` in the future
    // We add 1 to account for the remainder of time left until the next tick arrives,
    // which is anywhere between [0,1) ticks. Ie the +1 has the effect of 'burning off'
    // this remainder time, so we can safely add `ticks` to that result, and guarantee
    // that we get a deadline that's at least `ticks` in the future.
    // Ints must be disabled
    static Deadline _DeadlineForTicks(Ticks ticks) {
        return _ISR.CurrentTime+ticks+1;
    }
    
    template<typename T>
    static T _TFromPtr(uintptr_t x) {
        static_assert(sizeof(T) <= sizeof(uintptr_t));
        union { T t; uintptr_t ptr; } u = { .ptr = x };
        return u.t;
    }
    
    template<typename T>
    static uintptr_t _PtrFromT(T x) {
        static_assert(sizeof(T) <= sizeof(uintptr_t));
        union { T t; uintptr_t ptr; } u = { .t = x };
        return u.ptr;
    }
    
    // __TaskSwap(): architecture-specific function that saves the stack pointer into
    // _TaskPrev->sp and restores the stack pointer to _TaskCurr->sp. Steps:
    //
    //   (1) Push callee-saved regs onto stack (including $PC if needed for step #5 to work)
    //   (2) Save $SP into `_TaskPrev->sp`
    //   (3) Restore $SP from `_TaskCurr->sp`
    //   (4) Pop callee-saved registers from stack
    //   (5) Return to caller
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void __TaskSwap() {
#if defined(__MSP430__) && !defined(__LARGE_CODE_MODEL__)
        // MSP430, small memory model
        static_assert(sizeof(void*) == 2);
        #define _SchedulerStackAlign            1   // Count of pointer-sized registers to which the stack needs to be aligned
        #define _SchedulerStackSaveRegCount     7   // Count of pointer-sized registers that we save below (excluding $PC)
        asm volatile("pushm #7, r10" : : : );                           // (1)
        asm volatile("mov sp, %0" : "=m" (_TaskPrev->sp) : : );         // (2)
        asm volatile("mov %0, sp" : : "m" (_TaskCurr->sp) : );          // (3)
        asm volatile("popm #7, r10" : : : );                            // (4)
        asm volatile("ret" : : : );                                     // (5)
#elif defined(__MSP430__) && defined(__LARGE_CODE_MODEL__)
        // MSP430, large memory model
        static_assert(sizeof(void*) == 4);
        #define _SchedulerStackAlign            1   // Count of pointer-sized registers to which the stack needs to be aligned
        #define _SchedulerStackSaveRegCount     7   // Count of pointer-sized registers that we save below (excluding $PC)
        asm volatile("pushm.a #7, r10" : : : );                         // (1)
        asm volatile("mov.a sp, %0" : "=m" (_TaskPrev->sp) : : );       // (2)
        asm volatile("mov.a %0, sp" : : "m" (_TaskCurr->sp) : );        // (3)
        asm volatile("popm.a #7, r10" : : : );                          // (4)
        asm volatile("ret.a" : : : );                                   // (5)
#elif defined(__arm__)
        // ARM32
        static_assert(sizeof(void*) == 4);
        #define _SchedulerStackAlign            1   // Count of pointer-sized registers to which the stack needs to be aligned
        #define _SchedulerStackSaveRegCount     8   // Count of pointer-sized registers that we save below (excluding $PC)
        asm volatile("push {r4-r11,lr}" : : : );                        // (1)
        asm volatile("str sp, %0" : "=m" (_TaskPrev->sp) : : );         // (2)
        asm volatile("ldr sp, %0" : : "m" (_TaskCurr->sp) : );          // (3)
        asm volatile("pop {r4-r11,pc}" : : : );                         // (4)
#elif defined(__x86_64__)
        // AMD64
        static_assert(sizeof(void*) == 8);
        #define _SchedulerStackAlign            2   // Count of pointer-sized registers to which the stack needs to be aligned
        #define _SchedulerStackSaveRegCount     6   // Count of pointer-sized registers that we save below (excluding $PC)
        asm volatile("push %%rbx" : : : );                              // (1)
        asm volatile("push %%rbp" : : : );                              // (1)
        asm volatile("push %%r12" : : : );                              // (1)
        asm volatile("push %%r13" : : : );                              // (1)
        asm volatile("push %%r14" : : : );                              // (1)
        asm volatile("push %%r15" : : : );                              // (1)
        asm volatile("mov %%rsp, %0" : "=m" (_TaskPrev->sp) : : );      // (2)
        asm volatile("mov %0, %%rsp" : : "m" (_TaskCurr->sp) : );       // (3)
        asm volatile("pop %%r15" : : : );                               // (4)
        asm volatile("pop %%r14" : : : );                               // (4)
        asm volatile("pop %%r13" : : : );                               // (4)
        asm volatile("pop %%r12" : : : );                               // (4)
        asm volatile("pop %%rbp" : : : );                               // (4)
        asm volatile("pop %%rbx" : : : );                               // (4)
        asm volatile("ret" : : : );                                     // (5)
#else
        #error Task: Unsupported architecture
#endif
    }
    
    // MARK: - Stack Guard
    static void _StackGuardInit() {
        // Initialize each task's stack guard
        if constexpr (_StackGuardEnabled) {
            for (_Task& task : _Tasks) {
                _StackGuardInit(*task.stackGuard);
            }
        }
        
        // Initialize the interrupt stack guard
        if constexpr (_InterruptStackGuardEnabled) _StackGuardInit(_InterruptStackGuard);
    }
    
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
    
    static bool _TaskNext() {
        _TaskPrev = _TaskCurr;
        for (;;) {
            _TaskCurr = _TaskCurr->next;
            if (_TaskCurr->runnable()) return true;
            if (_TaskCurr == _TaskPrev) return false;
        }
    }
    
    // _TaskSwap(): saves _TaskCurr and restores the next runnable task
    // Ints must be disabled
    [[gnu::noinline]]
    static void _TaskSwap(_RunnableFn fn, std::optional<Deadline> wake=std::nullopt) {
        // Check stack guards
        if constexpr (_StackGuardEnabled) _StackGuardCheck(*_TaskCurr->stackGuard);
        if constexpr (_InterruptStackGuardEnabled) _StackGuardCheck(_InterruptStackGuard);
        
        // Update _TaskCurr's state
        _TaskCurr->runnable = fn;
        _TaskCurr->wakeDeadline = wake;
        if (wake) _ISR.WakeDeadlineUpdate = true;
        
        // Get the next runnable task, or sleep if no task wants to run
        while (!_TaskNext()) {
            T_Sleep();
            // Let interrupts fire after waking
            IntState ints(true);
        }
        
        __TaskSwap();
    }
    
    static bool _RunnableTrue() {
        return true;
    }
    
    static bool _RunnableFalse() {
        return false;
    }
    
    // _TaskGet(): returns the _Task& for the given T_Task
    template<typename T_Task>
    static constexpr _TaskFn _Task0RunFn() {
        static_assert((std::is_same_v<T_Task, T_Tasks> || ...), "invalid task");
        constexpr size_t idx = _ElmIdx<T_Task, T_Tasks...>();
        if constexpr (idx == 0) {
            return T_Task::Run;
        }
        return nullptr;
    }
    
    // _TaskGet(): returns the _Task& for the given T_Task
    template<typename T_Task, size_t T_Delta=0>
    static constexpr _Task& _TaskGet() {
        static_assert((std::is_same_v<T_Task, T_Tasks> || ...), "invalid task");
        constexpr size_t idx = (_ElmIdx<T_Task, T_Tasks...>() + T_Delta) % std::size(_Tasks);
        return _Tasks[idx];
    }
    
    template<typename T_1, typename T_2=void, typename... T_s>
    static constexpr size_t _ElmIdx() {
        return std::is_same_v<T_1,T_2> ? 0 : 1 + _ElmIdx<T_1, T_s...>();
    }
    
    static inline _Task _Tasks[sizeof...(T_Tasks)] = {
        _Task{
            .run        = _Task0RunFn<T_Tasks>(),
            .runnable   = _RunnableFalse,
            .sp         = nullptr,
            .stackGuard = (_StackGuard*)T_Tasks::Stack,
            .next       = &_TaskGet<T_Tasks, 1>(),
        }...,
    };
    
    static constexpr bool _StackGuardEnabled = (bool)T_StackGuardCount;
    static constexpr bool _InterruptStackGuardEnabled =
        _StackGuardEnabled &&
        !std::is_null_pointer_v<decltype(T_StackInterrupt)>;
    
    // _InterruptStackGuard: ideally this would be `static constexpr` instead of
    // `static inline`, but C++ doesn't allow constexpr reinterpret_cast.
    // In C++20 we could use std::bit_cast for this.
    static inline _StackGuard& _InterruptStackGuard = *(_StackGuard*)T_StackInterrupt;
    static inline _Task* _TaskPrev = nullptr;
    static inline _Task* _TaskCurr = &_Tasks[0];
    
    #warning TODO: these should be volatile no?
    static inline struct {
        Ticks CurrentTime = 0;
        std::optional<Deadline> WakeDeadline;
        bool WakeDeadlineUpdate = false;
    } _ISR;
};

} // namespace Toastbox
