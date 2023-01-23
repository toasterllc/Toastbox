#pragma once
#include <type_traits>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <algorithm>
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

// MARK: - TaskSwap

// _SchedulerTaskSwap(): architecture-specific macro that swaps the current task
// with a different task. Steps:
//
// (1) Push callee-saved regs onto stack (including $PC if needed for step #4 to work)
// (2) Swap $SP (stack pointer) and `sp` (macro argument)
// (3) Pop callee-saved registers from stack
// (4) Return to caller

#if defined(SchedulerMSP430)

#define _SchedulerTaskSwap(initFn, sp)                                                  \
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

#elif defined(SchedulerARM32)

#define _SchedulerTaskSwap(initFn, sp)                                                  \
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
    
#elif defined(SchedulerAMD64)

#define _SchedulerStackAlign            2   // Count of pointer-sized registers to which the stack needs to be aligned
#define _SchedulerStackSaveRegCount     6   // Count of pointer-sized registers that _SchedulerTaskSwap saves
#define _SchedulerTaskSwap(sp)                                                          \
                                                                                        \
    /* ## Architecture = AMD64 */                                                       \
    asm volatile("push %%rbx" : : : );                                  /* (1) */       \
    asm volatile("push %%rbp" : : : );                                  /* (1) */       \
    asm volatile("push %%r12" : : : );                                  /* (1) */       \
    asm volatile("push %%r13" : : : );                                  /* (1) */       \
    asm volatile("push %%r14" : : : );                                  /* (1) */       \
    asm volatile("push %%r15" : : : );                                  /* (1) */       \
    asm volatile("mov %%rsp, %%rbx" : : : "rbx");                       /* (2) */       \
    asm volatile("mov %0, %%rsp" : : "m" (sp) : );                      /* (2) */       \
    asm volatile("mov %%rbx, %0" : "=m" (sp) : : );                     /* (2) */       \
    asm volatile("pop %%r15" : : : );                                   /* (3) */       \
    asm volatile("pop %%r14" : : : );                                   /* (3) */       \
    asm volatile("pop %%r13" : : : );                                   /* (3) */       \
    asm volatile("pop %%r12" : : : );                                   /* (3) */       \
    asm volatile("pop %%rbp" : : : );                                   /* (3) */       \
    asm volatile("pop %%rbx" : : : );                                   /* (3) */       \
    asm volatile("ret" : : : );                                         /* (4) */       \
    
#else
    
    #error Task: Unsupported architecture
    
#endif

// MARK: - Scheduler

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

public:
    using Ticks     = unsigned int;
    using Deadline  = Ticks;

private:
    struct _Task;
    
//    template <typename T>
//    struct _List {
//        T* prev = this;
//        T* next = this;
//    };
    
    struct _ListRunSleep {
        _ListRunSleep* prev = this;
        _ListRunSleep* next = this;
    };
    
    struct _ListChannel {
        _ListChannel* prev = this;
        _ListChannel* next = this;
    };
    
    using _StackGuard = uintptr_t[T_StackGuardCount];
    
    using _TaskFn = void(*)();
    
    struct _Task : _ListRunSleep, _ListChannel {
        _TaskFn run = nullptr;
//        TaskFn cont = nullptr;
        void* sp = nullptr;
        Deadline wake = 0;
        _StackGuard& stackGuard;
    };
    
public:
    
    // MARK: - Channel
    
    template <typename T_Type, size_t T_Cap=0>
    class Channel {
    public:
        using Type = T_Type;
        static constexpr size_t Cap = T_Cap;
        
    private:
        T_Type _q[std::max((size_t)1, T_Cap)];
        size_t _w = 0;
        size_t _r = 0;
        bool _full = false;
        _ListChannel _senders;
        _ListChannel _receivers;
    };
    
//    struct _SchedulerTask {
//        static void Run() {
//    //        for (volatile int i=0; i<5; i++) {
//    //            
//    //        }
//            for (;;) {
//                printf("_TaskA\n");
//                _Scheduler::Sleep(_Scheduler::Ms(500));
//    //            usleep(1000);
//            }
//        }
//        
//        // Task stack
//        alignas(_SchedulerStackAlign*sizeof(void*))
//        static inline uint8_t Stack[4096];
//    };
    
//    // Start<task,fn>(): starts `task` running with `fn`
//    template <typename T_Task, typename T_Fn>
//    static void Start(T_Fn&& fn) {
//        constexpr _Task& task = _GetTask<T_Task>();
//        task.run = fn;
//        task.cont = _TaskSwapInit;
//        task.sp = T_Task::Stack + sizeof(T_Task::Stack);
//    }
//    
//    // Stop<task>(): stops `task`
//    template <typename T_Task>
//    static void Stop() {
//        constexpr _Task& task = _GetTask<T_Task>();
//        task.cont = _TaskNop;
//    }
//    
//    // Running<task>(): returns whether `task` is running
//    template <typename T_Task>
//    static bool Running() {
//        constexpr _Task& task = _GetTask<T_Task>();
//        return task.cont != _TaskNop;
//    }
    
    // Run(): run the tasks indefinitely
    [[noreturn]]
    static void Run() {
        // Initialize the main stack guard
        if constexpr ((bool)T_StackGuardCount) {
            _StackGuardInit(_MainStackGuard);
        }
        
        // Prepare every task
        for (_Task& task : _Tasks) {
            // Initialize the task's stack guard
            if constexpr ((bool)T_StackGuardCount) {
                _StackGuardInit(task.stackGuard);
            }
            
            const size_t extra = (_SchedulerStackSaveRegCount+1) % _SchedulerStackAlign;
            void**& sp = *((void***)&task.sp);
            // Push extra slots to ensure `_SchedulerStackAlign` alignment
            sp -= extra;
            // Push initial return address == task.run address == Task::Run
            sp--;
            *sp = (void*)task.run;
            // Push registers that _SchedulerTaskSwap() expects to be saved on the stack.
            // We don't care about what values the registers contain since they're not actually used.
            sp -= _SchedulerStackSaveRegCount;
        }
        
        // Enable interrupts
        IntState::Set(true);
        
        for (;;) {
            for (_ListRunSleep* i=_ListRun.next; i!=&_ListRun;) {
                _TaskCurr = static_cast<_Task*>(i);
                _TaskCurrSleepType = _SleepType::None;
//                _TaskCurrSleep = false;
                _TaskSwap();
                
                // Check stack guards
                if constexpr ((bool)T_StackGuardCount) {
                    _StackGuardCheck(_MainStackGuard);
                    _StackGuardCheck(_TaskCurr->stackGuard);
                }
                
                i = i->next;
                
                // Disable interrupts while we update our data structures
                IntState ints(false);
                switch (_TaskCurrSleepType) {
                case _SleepType::None:
                    break;
                
                case _SleepType::Indefinite: {
                    // Sleep `_TaskCurr` indefinitely by detaching it from
                    // the runnable list of tasks
                    _Detach(static_cast<_ListRunSleep&>(*_TaskCurr));
//            _Attach(_ListRun, static_cast<_ListRunSleep&>(_TaskCurr));
                    
                    
//                    // Sleep `_TaskCurr` indefinitely by detaching it from
//                    // the runnable list of tasks
//                    const _ListRunSleep& x = static_cast<_ListRunSleep&>(*_TaskCurr);
//                    _ListRunSleep& l = *x.prev;
//                    _ListRunSleep& r = *x.next;
//                    l.next = &r;
//                    r.prev = &l;
                    break;
                }
                
                case _SleepType::Deadline: {
                    _Detach(static_cast<_ListRunSleep&>(*_TaskCurr));
                    
                    // Find where to insert the task
                    _ListRunSleep* insert = &_ListSleep;
                    Ticks delta = _TaskCurr->wake - _ISR.CurrentTime;
                    for (_ListRunSleep* i=_ListSleep.next; i!=&_ListSleep; i=i->next) {
                        _Task& t = static_cast<_Task&>(*i);
                        Ticks d = t.wake - _ISR.CurrentTime;
                        if (delta > d) break;
                        insert = i;
                    }
                    
                    _Attach(_ListSleep, static_cast<_ListRunSleep&>(_TaskCurr));
                    break;
                }}
            }
            
            // Reset _ISR.Wake now that we're assured that every task has been able to observe
            // _ISR.Wake=true while ints were disabled during the entire process.
            // (If ints were enabled, it's because we entered a task, and therefore
            // _DidWork=true. So if we get here, it's because _DidWork=false -> no tasks
            // were entered -> ints are still disabled.)
            _ISR.Wake = false;
            
            // If `_ListRun` is empty (ie there are no runnable tasks), go to sleep
            if (_ListRun.next == &_ListRun) {
                T_Sleep();
            }
            
//            // Run tasks until the runnable list is empty
//            while (_ListRun.next != &_ListRun) {
////                _Task** tasksRunning = &_TasksRun;
//                
////                // End of task list
////                *tasksRunning = nullptr;
//            }
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
        }
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        // Return to scheduler
        _TaskSwap();
    }
    
//    // Wait(fn): sleep current task until `fn` returns true.
//    // `fn` must not cause any task to become runnable.
//    // If it does, the scheduler may not notice that the task is runnable and
//    // could go to sleep instead of running the task.
//    //
//    // If ints are disabled before calling Wait(), ints are guaranteed to have
//    // remained disabled between `fn` executing and Wait() returning, and
//    // therefore the condition that `fn` checks is guaranteed to remain true
//    // after Wait() returns.
//    //
//    // Ints are disabled while calling `fn`
//    template <typename T_Fn>
//    static auto Wait(T_Fn&& fn) {
//        // IntState:
//        //   - ints must be disabled when calling `fn`
//        //   - int state must be restored upon return because scheduler clobbers it
//        IntState ints(false);
//        for (;;) {
//            const auto r = fn();
//            if (!r) {
//                _TaskSwap();
//                continue;
//            }
//            return r;
//        }
//    }
    
//    // Wait(): sleep current task until `fn` returns true, or `ticks` have passed.
//    // See Wait() function above for more info.
//    template <typename T_Fn>
//    static auto Wait(Ticks ticks, T_Fn&& fn) {
//        // IntState:
//        //   - ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.CurrentTime
//        //   - ints must be disabled because _WaitUntil() requires it
//        //   - int state must be restored upon return because scheduler clobbers it
//        IntState ints(false);
//        const Deadline deadline = _ISR.CurrentTime+ticks+1;
//        return _WaitUntil(deadline, std::forward<T_Fn>(fn));
//    }
//    
//    // WaitUntil(): wait for a condition to become true, or for a deadline to pass.
//    //
//    // For a deadline to be considered in the past, it must be in the range:
//    //   [CurrentTime - TicksMax/2, CurrentTime]
//    // For a deadline to be considered in the future, it must be in the range:
//    //   [CurrentTime+1, CurrentTime + TicksMax/2 + 1]
//    //
//    // where TicksMax is the maximum value that the `Ticks` type can hold.
//    //
//    // See comment in function body for more info regarding the deadline parameter.
//    //
//    // See Wait() function above for more info.
//    template <typename T_Fn>
//    static auto WaitUntil(Deadline deadline, T_Fn&& fn) {
//        // IntState:
//        //   - ints must be disabled to prevent racing against Tick() ISR in accessing _ISR.CurrentTime
//        //   - ints must be disabled because _WaitUntil() requires it
//        //   - int state must be restored upon return because scheduler clobbers it
//        IntState ints(false);
//        
//        // Test whether `deadline` has already passed.
//        //
//        // Because _ISR.CurrentTime rolls over periodically, it's impossible to differentiate
//        // between `deadline` passing versus merely being far in the future. (For example,
//        // consider the case where time is tracked with a uint8_t: if Deadline=127 and
//        // CurrentTime=128, either Deadline passed one tick ago, or Deadline will pass
//        // 255 ticks in the future.)
//        //
//        // To solve this ambiguity, we require deadlines to be within
//        // [-TicksMax/2, +TicksMax/2+1] of _ISR.CurrentTime (where TicksMax is the maximum
//        // value that the `Ticks` type can hold), which allows us to employ the following
//        // heuristic:
//        //
//        // For a deadline to be considered in the past, it must be in the range:
//        //   [CurrentTime - TicksMax/2, CurrentTime]
//        // For a deadline to be considered in the future, it must be in the range:
//        //   [CurrentTime+1, CurrentTime + TicksMax/2 + 1]
//        //
//        // Now that ints are disabled (and therefore _ISR.CurrentTime is unchanging), we
//        // can employ the above heuristic to determine whether `deadline` has already passed.
//        constexpr Ticks TicksMax = std::numeric_limits<Ticks>::max();
//        const bool deadlinePassed = _ISR.CurrentTime-deadline <= TicksMax/2;
//        if (deadlinePassed) {
//            return std::optional<std::invoke_result_t<T_Fn>>{};
//        }
//        return _WaitUntil(deadline, std::forward<T_Fn>(fn));
//    }
//    
//    // Wait<tasks>(): sleep current task until `tasks` all stop running
//    template <typename... T_Tsks>
//    static void Wait() {
//        Wait([] { return (!Running<T_Tsks>() && ...); });
//    }
    
    
    
    
    
//    // Unbuffered send
//    template <
//    typename T,
//    size_t _T_Cap = T::Cap,
//    typename std::enable_if_t<_T_Cap==0, int> = 0
//    >
//    static void Send(T& chan, const typename T::Type& val) {
//        IntState ints(false);
//        for (;;) {
//            // If a reader is already waiting, set the value and wake it
//            if (chan._reader) {
//                chan._q[0] = val;
//                _TaskWake(chan._reader);
//                return;
//            }
//            
//            // Otherwise, there's no reader waiting.
//            // If a writer doesn't already exist, make ourself the writer and wait for the reader to wake us.
//            if (!chan._writer) {
//                chan._q[0] = val;
//                chan._writer = _TaskCurr;
//                _TaskSleep();
//                chan._writer = nullptr;
//                if (chan._writerNext) {
//                    _TaskWake(chan._writerNext);
//                }
//                return;
//            }
//            
//            #warning TODO: we should the _Task linked list so that if a _Task bails from reading/writing (due to a timeout), it can remove itself from the linked list. with the current solution, the bailing task can't remove itself if another task 'steals' the _writerNext slot
//            // There's already a writer
//            // Steal the chan._writerNext slot, and restore it when someone wakes us
//            _Task* writerNext = chan._writerNext;
//            chan._writerNext = _TaskCurr;
//            _TaskSleep();
//            chan._writerNext = writerNext;
//        }
//    }
//    
//    // Unbuffered receive
//    template <
//    typename T,
//    size_t _T_Cap = T::Cap,
//    typename std::enable_if_t<_T_Cap==0, int> = 0
//    >
//    static typename T::Type Recv(T& chan) {
//        IntState ints(false);
//        for (;;) {
//            // If a writer is already waiting, get the value and wake it
//            if (chan._writer) {
//                _TaskWake(chan._writer);
//                return chan._q[0];
//            }
//            
//            // Otherwise, there's no writer waiting.
//            // If a reader doesn't already exist, make ourself the reader and wait for the writer to wake us.
//            if (!chan._reader) {
//                chan._reader = _TaskCurr;
//                _TaskSleep();
//                chan._reader = nullptr;
//                if (chan._readerNext) {
//                    _TaskWake(chan._readerNext);
//                }
//                return;
//            }
//            
//            
//            #warning TODO: we should the _Task linked list so that if a _Task bails from reading/writing (due to a timeout), it can remove itself from the linked list. with the current solution, the bailing task can't remove itself if another task 'steals' the _readerNext slot
//            // There's already a reader
//            // Steal the chan._readerNext slot, and restore it when someone wakes us
//            _Task* readerNext = chan._readerNext;
//            chan._readerNext = _TaskCurr;
//            _TaskSleep();
//            chan._readerNext = readerNext;
//        }
//    }
    
    template <typename T>
    static void _Detach(T& x) {
        T& l = *x.prev;
        T& r = *x.next;
        l.next = &r;
        r.prev = &l;
    }
    
    template <typename T>
    static void _Attach(T& l, T& x) {
        T& r = *_ListRun.next;
        x.prev = &l;
        x.next = &r;
        l.next = &x;
        r.prev = &x;
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
                if (chan._w == T::Cap) chan._w = 0;
                if (chan._w == chan._r) chan._full = true;
                
                // If there's a reader, wake it
                if (chan._reader) {
                    _TaskWake(chan._reader);
                }
                return;
            }
            
//            _Detach(static_cast<_ListRunSleep&>(_TaskCurr));
//            _Attach(_ListRun, static_cast<_ListRunSleep&>(_TaskCurr));
            
            #warning TODO: we should the _Task linked list so that if a _Task bails from reading/writing (due to a timeout), it can remove itself from the linked list. with the current solution, the bailing task can't remove itself if another task 'steals' the _writer slot
            
            _TaskCurr->list.channel = chan._senders;
            chan._senders = _TaskCurr;
            bool ok = _TaskSleep();
            if (!ok) {
                // Timeout
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
                const typename T::Type& val = chan._q[chan._r];
                chan._r++;
                if (chan._r == T::Cap) chan._r = 0;
                chan._full = false;
                
                // If there's a writer, wake it
                if (chan._writer) {
                    _TaskWake(chan._writer);
                }
                return val;
            }
            
            #warning TODO: we should the _Task linked list so that if a _Task bails from reading/writing (due to a timeout), it can remove itself from the linked list. with the current solution, the bailing task can't remove itself if another task 'steals' the _reader slot
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
        _TaskSleep(_ISR.CurrentTime+ticks+1);
        
//        const Deadline deadline = _ISR.CurrentTime+ticks+1;
//        do {
//            // Update _ISR.WakeDeadline
//            _ProposeWakeDeadline(deadline);
//            
//            // Wait until we wake because _ISR.WakeDeadline expired (not necessarily
//            // because of this task though)
//            do _TaskSwap();
//            while (!_ISR.Wake);
//        
//        } while (_ISR.CurrentTime != deadline);
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
    
    // _TaskSleep(): sleep current task indefinitely
    static void _TaskSleep() {
        _TaskCurrSleepType = _SleepType::Indefinite;
        // Return to scheduler
        _TaskSwap();
    }
    
    // _TaskSleep(deadline): sleep current task until `wake`.
    // Returns true if the task awoke early due to another task waking it.
    static bool _TaskSleep(Deadline wake) {
        _TaskCurrSleepType = _SleepType::Deadline;
        _TaskCurr->wake = wake;
        // Return to scheduler
        _TaskSwap();
        #warning TODO: implement real return value
        return true;
    }
    
    #warning TODO: if the task was sleeping until a given deadline, do we need to do anything special here?
    // _TaskWake: insert the given task into the running list
    static void _TaskWake(_Task* task) {
        // Detach task from whatever lists it's a part of
        _Detach(static_cast<_ListRunSleep&>(*task));
        _Detach(static_cast<_ListChannel&>(*task));
        // Insert task into the beginning of the runnable list (_ListRun)
        _Attach(_ListRun, static_cast<_ListRunSleep&>(*task));
        
//        _ListRun.next = task;
//        
//        task->next = ;
//        _ListRun.next = 
//        
//        #warning TODO: implement
//        // Insert task into the running list
//        task->next = _TasksRun;
//        _TasksRun = task;
    }
    
//    [[noreturn]]
//    static void _TaskRun() {
//        // Enable ints when initially entering a task.
//        // We're not using an IntState here because this function never actually returns (since
//        // we call _TaskSwap() at the end to return to the scheduler) and therefore we don't
//        // need IntState's dtor cleanup behavior.
//        IntState::Set(true);
////        // Future invocations should invoke _TaskSwap
////        _TaskCurr->cont = _TaskSwap;
//        // Invoke task function
//        _TaskCurr->run();
//        // Tasks should never return
//        Assert(false);
//        
////        // The task finished
////        // Future invocations should do nothing
////        _TaskCurr->cont = _TaskNop;
//        // Return to scheduler
////        for (;;) _TaskSwap();
//    }
    
//    // _TaskSwapInit(): swap task in and jump to _TaskRun
//    // Ints must be disabled
//    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
//    static void _TaskSwapInit() {
//        _SchedulerTaskSwap(_TaskRun, _TaskCurr->sp);
//    }
    
    // _TaskSwap(): swaps the current task and the saved task
    static void _TaskSwap() {
        IntState ints; // Save/restore interrupt state
        __TaskSwap();
    }
    
    // __TaskSwap(): swaps the current task and the saved task
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void __TaskSwap() {
        _SchedulerTaskSwap(_TaskCurr->sp);
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
//    
//    // _WaitUntil(): wait for a condition to become true, or for a deadline to pass.
//    // Ints must be disabled
//    template <typename T_Fn>
//    static auto _WaitUntil(Deadline deadline, T_Fn&& fn) {
//        do {
//            const auto r = fn();
//            if (r) {
//                return std::make_optional(r);
//            }
//            
//            // Update _ISR.WakeDeadline
//            _ProposeWakeDeadline(deadline);
//            
//            // Next task
//            _TaskSwap();
//        } while (_ISR.CurrentTime != deadline);
//        
//        // Timeout
//        return std::optional<std::invoke_result_t<T_Fn>>{};
//    }
    
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
    
    static constexpr size_t _TaskCount = sizeof...(T_Tasks);
    
    template <size_t... T_Idx>
    static constexpr std::array<_Task,_TaskCount> _TasksGet(std::integer_sequence<size_t, T_Idx...>) {
        return {
            _Task{
                _ListRunSleep{
                    .prev = (T_Idx==0 ?             &_ListRun : &_Tasks[T_Idx-1]),
                    .next = (T_Idx==_TaskCount-1 ?  &_ListRun : &_Tasks[T_Idx+1]),
                },
                .run        = T_Tasks::Run,
//                .cont       = _TaskSwapInit,
                .sp         = T_Tasks::Stack + sizeof(T_Tasks::Stack),
                .stackGuard = *(_StackGuard*)T_Tasks::Stack,
            }...,
        };
    }
    
    static constexpr std::array<_Task,_TaskCount> _TasksGet() {
        return _TasksGet(std::make_integer_sequence<size_t, _TaskCount>{});
    }
    
    static inline std::array<_Task,_TaskCount> _Tasks = _TasksGet();
    
    // _MainStackGuard: ideally this would be `static constexpr` instead of `static inline`,
    // but C++ doesn't allow constexpr reinterpret_cast.
    // In C++20 we could use std::bit_cast for this.
    static inline _StackGuard& _MainStackGuard = *(_StackGuard*)T_MainStack;
    
//    static inline struct {
//        _TaskList run = &_Tasks[0];
//        _TaskList sleep = nullptr;
//    } _List;
    
    enum class _SleepType {
        None,
        Indefinite,
        Deadline,
    };
    
    static inline _Task* _TaskCurr = nullptr;
    static inline _SleepType _TaskCurrSleepType = _SleepType::None;
//    static inline Deadline _TaskCurrSleepDeadline = 0;
    static inline _ListRunSleep _ListRun = {
        .prev = static_cast<_ListRunSleep*>(&_Tasks[_TaskCount-1]),
        .next = static_cast<_ListRunSleep*>(&_Tasks[0]),
    };
    static inline _ListRunSleep _ListSleep;
    
    static volatile inline struct {
        Ticks CurrentTime = 0;
        Deadline WakeDeadline = 0;
        bool Wake = false;
    } _ISR;
#undef Assert
};

} // namespace Toastbox
