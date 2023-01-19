#pragma once
#include <type_traits>
#include <cstdint>
#include <cstdlib>
#include <optional>

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

// MARK: - TaskSwap

// TaskSwap(): architecture-specific macro that swaps the current task
// with a different task. Steps:
//
// (1) Push callee-saved regs onto stack, including $PC if needed
// (2) tmp = $SP
// (3) $SP = `sp`
// (4) `sp` = tmp
// if `initFn` != nullptr:
//   (5) Jump to `initFn`
// else
//   (6) Pop callee-saved registers from stack
//   (7) Return to caller

#if defined(TaskMSP430)

#define TaskSwap(initFn, sp)                                                            \
                                                                                        \
    if constexpr (sizeof(void*) == 2) {                                                 \
        /* ## Architecture = MSP430, small memory model */                              \
        asm volatile("pushm #7, r10" : : : );                           /* (1) */       \
        asm volatile("mov sp, r11" : : : "r11");                        /* (2) */       \
        asm volatile("mov %0, sp" : : "m" (sp) : );                     /* (3) */       \
        asm volatile("mov r11, %0" : "=m" (sp) : : );                   /* (4) */       \
        if constexpr (!std::is_null_pointer<decltype(initFn)>::value) {                 \
            asm volatile("br %0" : : "i" (initFn) : );                  /* (5) */       \
        } else {                                                                        \
            asm volatile("popm #7, r10" : : : );                        /* (6) */       \
            asm volatile("ret" : : : );                                 /* (7) */       \
        }                                                                               \
    } else {                                                                            \
        /* ## Architecture = MSP430, large memory model */                              \
        asm volatile("pushm.a #7, r10" : : : );                         /* (1) */       \
        asm volatile("mov.a sp, r11" : : : "r11");                      /* (2) */       \
        asm volatile("mov.a %0, sp" : : "m" (sp) : );                   /* (3) */       \
        asm volatile("mov.a r11, %0" : "=m" (sp) : : );                 /* (4) */       \
        if constexpr (!std::is_null_pointer<decltype(initFn)>::value) {                 \
            asm volatile("br.a %0" : : "i" (initFn) : );                /* (5) */       \
        } else {                                                                        \
            asm volatile("popm.a #7, r10" : : : );                      /* (6) */       \
            asm volatile("ret.a" : : : );                               /* (7) */       \
        }                                                                               \
    }

#elif defined(TaskARM32)

#define TaskSwap(initFn, sp)                                                            \
                                                                                        \
    /* ## Architecture = ARM32 */                                                       \
    asm volatile("push {r4-r11,lr}" : : : );                            /* (1) */       \
    asm volatile("mov r0, sp" : : : );                                  /* (2) */       \
                                                                                        \
    /* Toggle between the main stack pointer (MSP) and process stack pointer (PSP)      \
       when swapping tasks:                                                             \
                                                                                        \
        Entering task (exiting scheduler): use PSP                                      \
        Entering scheduler (exiting task): use MSP                                      \
                                                                                        \
      This makes the hardware enforce separation between the single main stack          \
      (used for running the scheduler + handling interrupts) and the tasks'             \
      stacks.                                                                           \
                                                                                        \
      This scheme is mainly important for interrupt handling: if an interrupt occurs    \
      while a task is running, it'll execute using the main stack, *not the task's      \
      stack*. This is important because the task's stack size can be much smaller       \
      than certain interrupt handlers require. (For example, the STM32 USB interrupt    \
      handlers need lots of stack space.) */                                            \
    asm volatile("mrs r1, CONTROL");  /* Load CONTROL register into r1 */               \
    asm volatile("eor.w	r1, r1, #2"); /* Toggle SPSEL bit */                            \
    asm volatile("msr CONTROL, r1");  /* Store CONTROL register into r1 */              \
    /* "When changing the stack pointer, software must use an ISB instruction           \
        immediately after the MSR instruction. This ensures that instructions after     \
        the ISB instruction execute using the new stack pointer" */                     \
    asm volatile("isb");                                                                \
                                                                                        \
    asm volatile("ldr sp, %0" : : "m" (sp) : );                         /* (3) */       \
    asm volatile("str r0, %0" : "=m" (sp) : : );                        /* (4) */       \
    if constexpr (!std::is_null_pointer<decltype(initFn)>::value) {                     \
        asm volatile("b %0" : : "i" (initFn) : );                       /* (5) */       \
    } else {                                                                            \
        asm volatile("pop {r4-r11,lr}" : : : );                         /* (6) */       \
        asm volatile("bx lr" : : : );                                   /* (7) */       \
    }
    
#else
    
    #error Task: Unsupported architecture
    
#endif

// MARK: - Scheduler

using TaskFn = void(*)();

struct TaskOptions {
    TaskFn AutoStart = nullptr;
};

template <
    uint32_t T_UsPerTick,               // T_UsPerTick: microseconds per tick
    void T_Sleep(),                     // T_Sleep: function to put processor to sleep; invoked when no tasks have work to do
    void T_Error(uint16_t),             // T_Error: function to call upon an unrecoverable error (eg stack overflow)
    auto T_MainStack,                   // T_MainStack: main stack pointer (only used to monitor main stack for overflow; unused if T_StackGuardCount==0)
    size_t T_StackGuardCount,           // T_StackGuardCount: number of pointer-sized stack guard elements to use
    typename... T_Tasks                 // T_Tasks: list of tasks
>
class Scheduler {
#define Assert(x) if (!(x)) T_Error(__LINE__)
private:
    struct _Task;
    
public:
    using Ticks     = unsigned int;
    using Deadline  = Ticks;
    
    // MARK: - Channel
    
    template <typename T_Type, size_t T_Cap=0>
    class Channel {
    public:
        using Type = T_Type;
        static constexpr size_t Cap = T_Cap;
        
    private:
        
    //    void _push(const T_Type& x) {
    //        _q[_w] = x;
    //        _w++;
    //        if (_w == T_Cap) _w = 0;
    //        if (_w == _r) _full = true;
    //    }
        
        T_Type _q[std::max((size_t)1, T_Cap)];
        size_t _w = 0;
        size_t _r = 0;
        bool _full = false;
        _Task* _writer = nullptr;
        _Task* _writerNext = nullptr;
        _Task* _reader = nullptr;
        _Task* _readerNext = nullptr;
    };
    
    // Start<task,fn>(): starts `task` running with `fn`
    template <typename T_Task, typename T_Fn>
    static void Start(T_Fn&& fn) {
        constexpr _Task& task = _GetTask<T_Task>();
        task.run = fn;
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
        // Initialize the main stack guard and each task's stack guard
        if constexpr (T_StackGuardCount) {
            _StackGuardInit(_MainStackGuard);
            for (_Task& task : _Tasks) {
                _StackGuardInit(task.stackGuard);
            }
        }
        
        for (;;) {
            while (_TasksRunnable) {
                _TaskCurr = _TasksRunnable;
                _TaskCurrPtr = &_TasksRunnable;
                
                do {
                    // Disable ints before entering a task.
                    // We're not using an IntState here because we don't want the IntState dtor
                    // cleanup behavior when exiting our scope; we want to keep ints disabled
                    // across tasks.
                    IntState::Set(false);
                    _TaskCurr->cont();
                    
                    // Check stack guards
                    if constexpr (T_StackGuardCount) {
                        _StackGuardCheck(_MainStackGuard);
                        _StackGuardCheck(_TaskCurr->stackGuard);
                    }
                    
                    _TaskCurr = _TaskCurr->next;
                    _TaskCurrPtr = &_TaskCurr->next;
                } while (_TaskCurr);
            }
            
            // Reset _ISR.Wake now that we're assured that every task has been able to observe
            // _ISR.Wake=true while ints were disabled during the entire process.
            // (If ints were enabled, it's because we entered a task, and therefore
            // _DidWork=true. So if we get here, it's because _DidWork=false -> no tasks
            // were entered -> ints are still disabled.)
            _ISR.Wake = false;
            
            // No work to do
            // Go to sleep!
            T_Sleep();
            
            // Allow ints to fire
            IntState::Set(true);
        }
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        // IntState:
        //   - int state must be restored upon return because scheduler clobbers it
        IntState ints;
        // Return to scheduler
        _TaskSwap();
        // Return to task
        _TaskStartWork();
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
    template <typename T_Fn>
    static auto Wait(T_Fn&& fn) {
        // IntState:
        //   - ints must be disabled when calling `fn`
        //   - int state must be restored upon return because scheduler clobbers it
        IntState ints(false);
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
    
    // Wait(): sleep current task until `fn` returns true, or `ticks` have passed.
    // See Wait() function above for more info.
    template <typename T_Fn>
    static auto Wait(Ticks ticks, T_Fn&& fn) {
        // IntState:
        //   - ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.CurrentTime
        //   - ints must be disabled because _WaitUntil() requires it
        //   - int state must be restored upon return because scheduler clobbers it
        IntState ints(false);
        const Deadline deadline = _ISR.CurrentTime+ticks+1;
        return _WaitUntil(deadline, std::forward<T_Fn>(fn));
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
    template <typename T_Fn>
    static auto WaitUntil(Deadline deadline, T_Fn&& fn) {
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
        constexpr Ticks TicksMax = std::numeric_limits<Ticks>::max();
        const bool deadlinePassed = _ISR.CurrentTime-deadline <= TicksMax/2;
        if (deadlinePassed) {
            return std::optional<std::invoke_result_t<T_Fn>>{};
        }
        return _WaitUntil(deadline, std::forward<T_Fn>(fn));
    }
    
    // Wait<tasks>(): sleep current task until `tasks` all stop running
    template <typename... T_Tsks>
    static void Wait() {
        Wait([] { return (!Running<T_Tsks>() && ...); });
    }
    
    
    
    
    
    // Unbuffered send
    template <
    typename T,
    size_t _T_Cap = T::Cap,
    typename std::enable_if_t<_T_Cap==0, int> = 0
    >
    static void Send(T& chan, const typename T::Type& val) {
        IntState ints(false);
        for (;;) {
            // If a reader is already waiting, set the value and wake it
            if (chan._reader) {
                chan._q[0] = val;
                _TaskWake(chan._reader);
                return;
            }
            
            // Otherwise, there's no reader waiting.
            // If a writer doesn't already exist, make ourself the writer and wait for the reader to wake us.
            if (!chan._writer) {
                chan._q[0] = val;
                chan._writer = _TaskCurr;
                _TaskSleep();
                chan._writer = nullptr;
                if (chan._writerNext) {
                    _TaskWake(chan._writerNext);
                }
                return;
            }
            
            // There's already a writer
            // Steal the chan._writerNext slot, and restore it when someone wakes us
            _Task* writerNext = chan._writerNext;
            chan._writerNext = _TaskCurr;
            _TaskSleep();
            chan._writerNext = writerNext;
        }
    }
    
    // Unbuffered receive
    template <
    typename T,
    size_t _T_Cap = T::Cap,
    typename std::enable_if_t<_T_Cap==0, int> = 0
    >
    static typename T::Type Recv(T& chan) {
        IntState ints(false);
        for (;;) {
            // If a writer is already waiting, get the value and wake it
            if (chan._writer) {
                _TaskWake(chan._writer);
                return chan._q[0];
            }
            
            // Otherwise, there's no writer waiting.
            // If a reader doesn't already exist, make ourself the reader and wait for the writer to wake us.
            if (!chan._reader) {
                chan._reader = _TaskCurr;
                _TaskSleep();
                chan._reader = nullptr;
                if (chan._readerNext) {
                    _TaskWake(chan._readerNext);
                }
                return;
            }
            
            // There's already a reader
            // Steal the chan._readerNext slot, and restore it when someone wakes us
            _Task* readerNext = chan._readerNext;
            chan._readerNext = _TaskCurr;
            _TaskSleep();
            chan._readerNext = readerNext;
        }
    }
    
    // Buffered send
    template <
    typename T,
    size_t _T_Cap = T::Cap,
    typename std::enable_if_t<_T_Cap!=0, int> = 0
    >
    static void Send(T& chan, const typename T::Type& val) {
        IntState ints(false);
        for (;;) {
            // If the channel has an available slot, add the value to the queue
            if (!chan._full) {
                chan._q[chan._w] = val;
                chan._w++;
                if (chan._w == T::Cap) _w = 0;
                if (chan._w == chan._r) chan._full = true;
                
                // If there's a reader, wake it
                if (chan._reader) {
                    _TaskWake(chan._reader);
                }
                return;
            }
            
            // Steal the chan._writer slot, and restore it when someone wakes us
            _Task* writerPrev = chan._writer;
            chan._writer = _TaskCurr;
            _TaskSleep();
            chan._writer = writerPrev;
        }
    }
    
    // Buffered receive
    template <
    typename T,
    size_t _T_Cap = T::Cap,
    typename std::enable_if_t<_T_Cap!=0, int> = 0
    >
    static typename T::Type Recv(T& chan) {
        IntState ints(false);
        for (;;) {
            // If the channel has available data, pop the value from the queue
            if (chan._w!=chan._r || chan._full) {
                const T::Type& val = chan._q[chan._r];
                chan._r++;
                if (chan._r == T::Cap) chan._r = 0;
                chan._full = false;
                
                // If there's a writer, wake it
                if (chan._writer) {
                    _TaskWake(chan._writer);
                }
                return val;
            }
            
            // Steal the chan._reader slot, and restore it when someone wakes us
            _Task* readerPrev = chan._reader;
            chan._reader = _TaskCurr;
            _TaskSleep();
            chan._reader = readerPrev;
        }
    }
    
    
    
    
    
    
    
    
    
    
    static constexpr Ticks Us(uint16_t us) { return _TicksForUs(us); }
    static constexpr Ticks Ms(uint16_t ms) { return _TicksForUs(1000*(uint32_t)ms); }
    
    // Sleep(ticks): sleep current task for `ticks`
    static void Sleep(Ticks ticks) {
        // IntState:
        //   - ints must be disabled to prevent racing against Tick() ISR in accessing _ISR
        //   - int state must be restored upon return because scheduler clobbers it
        IntState ints(false);
        
        const Deadline deadline = _ISR.CurrentTime+ticks+1;
        do {
            // Update _ISR.WakeDeadline
            _ProposeWakeDeadline(deadline);
            
            // Wait until we wake because _ISR.WakeDeadline expired (not necessarily
            // because of this task though)
            do _TaskSwap();
            while (!_ISR.Wake);
        
        } while (_ISR.CurrentTime != deadline);
        
        _TaskStartWork();
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
        // Don't increment time if there's an existing _ISR.Wake signal that hasn't been consumed.
        // This is necessary so that we don't miss any ticks, which could cause a task wakeup to be missed.
        if (_ISR.Wake) return true;
        
        _ISR.CurrentTime++;
        if (_ISR.CurrentTime == _ISR.WakeDeadline) {
            _ISR.Wake = true;
            return true;
        }
        
        return false;
    }
    
    static Ticks CurrentTime() {
        // IntState:
        //   - ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.CurrentTime
        IntState ints(false);
        return _ISR.CurrentTime;
    }
    
private:
    // MARK: - Types
    
    static constexpr uintptr_t _StackGuardMagicNumber = (uintptr_t)0xCAFEBABEBABECAFE;
    using _StackGuard = uintptr_t[T_StackGuardCount];
    
    struct _Task {
        TaskFn run = nullptr;
        TaskFn cont = nullptr;
        void* sp = nullptr;
        _Task* next = nullptr;
//        _Task* prev = nullptr;
//        bool runnable = false;
//        _Task* prev = nullptr;
//        _Task* next = nullptr;
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
    
    // _TaskSleep(): mark current task as sleeping and return to scheduler
    static void _TaskSleep() {
        // Remove current task from linked list of runnable tasks
        *_TaskCurrPtr = _TaskCurr->next;
        // Return to scheduler
        _TaskSwap();
        
//        #warning TODO: remove assert after debugging
//        Assert(_CurrentTask->runnable);
//        _CurrentTask->runnable = false;
//        
//        _CurrentTask->prev->next = _CurrentTask->next;
//        
//        if (_CurrentTask->prev) {
//            _CurrentTask->prev->next = 
//        } else {
//            _CurrentTask->prev
//        }
//        
//        _Task*& taskPrevPtr = 
//        
//        if ()
//        
//        _CurrentTask->next = 
//        
//        _CurrentTask->next = 
//        
//        _Runnable = _CurrentTask;
    }
    
    // _TaskWake: insert the given task into the runnable list
    static void _TaskWake(_Task* task) {
        // Insert task into the runnable list
        task->next = _TasksRunnable;
        _TasksRunnable = task;
        
//        #warning TODO: remove assert after debugging
//        Assert(!_CurrentTask->runnable);
    }
    
    static void _TaskStartWork() {
        _DidWork = true;
    }
    
    static void _TaskRun() {
        // Enable ints when initially entering a task.
        // We're not using an IntState here because this function never actually returns (since
        // we call _TaskSwap() at the end to return to the scheduler) and therefore we don't
        // need IntState's dtor cleanup behavior.
        IntState::Set(true);
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
    
    // _TaskSwapInit(): swap task in and jump to _TaskRun
    // Ints must be disabled
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void _TaskSwapInit() {
        TaskSwap(_TaskRun, _CurrentTask->sp);
    }
    
    // _TaskSwap(): swaps the current task and the saved task
    // Ints must be disabled
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void _TaskSwap() {
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
    
    // _ProposeWakeDeadline(): update _ISR.WakeDeadline with a new deadline,
    // if it occurs before the existing deadline.
    // Ints must be disabled
    static void _ProposeWakeDeadline(Deadline deadline) {
        const Ticks wakeDelay = deadline-_ISR.CurrentTime;
        const Ticks currentWakeDelay = _ISR.WakeDeadline-_ISR.CurrentTime;
        if (!currentWakeDelay || wakeDelay<currentWakeDelay) {
            _ISR.WakeDeadline = deadline;
        }
    }
    
    // _WaitUntil(): wait for a condition to become true, or for a deadline to pass.
    // Ints must be disabled
    template <typename T_Fn>
    static auto _WaitUntil(Deadline deadline, T_Fn&& fn) {
        do {
            const auto r = fn();
            if (r) {
                _TaskStartWork();
                return std::make_optional(r);
            }
            
            // Update _ISR.WakeDeadline
            _ProposeWakeDeadline(deadline);
            
            // Next task
            _TaskSwap();
        } while (_ISR.CurrentTime != deadline);
        
        // Timeout
        _TaskStartWork();
        return std::optional<std::invoke_result_t<T_Fn>>{};
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
    
//    using _TaskList = std::list<_Task*>;
//    static inline bool _DidWork = false;
//    static inline _Task* _CurrentTask = nullptr;
//    _TaskList _RunnableTasks;
//    _TaskList::iterator _CurrentTask;
    
    static inline _Task* _TasksRunnable = nullptr;
    static inline _Task* _TaskCurr = nullptr;
    static inline _Task** _TaskCurrPtr = &_TaskCurr;
    
    static volatile inline struct {
        Ticks CurrentTime = 0;
        Deadline WakeDeadline = 0;
        bool Wake = false;
    } _ISR;
#undef Assert
};

} // namespace Toastbox
