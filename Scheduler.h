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
    
    template <typename T>
    struct _List {
        T* prev = static_cast<T*>(this);
        T* next = static_cast<T*>(this);
        
        bool empty() const { return next==this; }
        
        template <typename T_Elm>
        void push(T_Elm& elm) {
            T& x = static_cast<T&>(elm);
            T& l = static_cast<T&>(*this);
            T& r = *l.next;
            x.prev = &l;
            x.next = &r;
            l.next = &x;
            r.prev = &x;
        }
        
        void pop() {
            T& l = *prev;
            T& r = *next;
            l.next = &r;
            r.prev = &l;
            prev = static_cast<T*>(this);
            next = static_cast<T*>(this);
        }
    };
    
    struct _ListRunSleep : _List<_ListRunSleep> {};
    struct _ListChannel : _List<_ListChannel> {};
    
    using _StackGuard = uintptr_t[T_StackGuardCount];
    
    using _TaskFn = void(*)();
    
    enum class _WakeReason : uint8_t {
        Event,      // An event occurred that caused the task to be awoken
        Deadline,   // The task's wake deadline arrived
    };
    
    struct _Task : _ListRunSleep, _ListChannel {
        _TaskFn run = nullptr;
        void* sp = nullptr;
        Deadline wakeDeadline = 0;
        _WakeReason wakeReason = _WakeReason::Event;
        _StackGuard& stackGuard;
        
        auto& listRunSleep() { return static_cast<_ListRunSleep&>(*this); }
        auto& listChannel() { return static_cast<_ListChannel&>(*this); }
    };
    
public:
    
    // MARK: - Channel
    
    template <typename T_Type, size_t T_Cap=0>
    class Channel {
    public:
        using Type = T_Type;
        static constexpr size_t Cap = T_Cap;
        
        bool empty() const { return _w==_r && !_full; }
        bool full() const { return _full; }
        
//    private:
        T_Type _q[std::max((size_t)1, T_Cap)];
        size_t _w = 0;
        size_t _r = 0;
        bool _full = false;
        _ListChannel _senders;
        _ListChannel _receivers;
    };
    
//    static void _CheckRunList() {
//        bool ok = false;
//        _ListRunSleep* test = _ListRun.next;
//        for (int i=0; i<10; i++) {
//            ok = (test == &_ListRun);
//            if (ok) return;
//            test = test->next;
//        }
//        abort();
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
                    // Detach `_TaskCurr` from the runnable list of tasks to sleep it
                    _TaskCurr->listRunSleep().pop();
                    break;
                }
                
                case _SleepType::Deadline: {
                    // Detach `_TaskCurr` from the runnable list of tasks
                    _TaskCurr->listRunSleep().pop();
                    
                    const Ticks delta = _TaskCurr->wakeDeadline - _ISR.CurrentTime;
                    _ListRunSleep* insert = &_ListDeadline;
                    for (;;) {
                        _ListRunSleep*const i = insert->next;
                        // If we're at the end of the list, we're done
                        if (i == &_ListDeadline) break;
                        const _Task& t = static_cast<_Task&>(*i);
                        const Ticks d = t.wakeDeadline - _ISR.CurrentTime;
                        // Use >= instead of > so that we attach the task at the earliest
                        // available slot, to minimize our computation.
                        if (delta >= d) break;
                        insert = i;
                    }
                    
                    // Attach `_TaskCurr` at the appropriate location in _ListDeadline,
                    // according to its wake time
//                    _Attach(*insert, *_TaskCurr);
                    
                    insert->push(*_TaskCurr);
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
            if (_ListRun.empty()) {
                T_Sleep();
            }
        }
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        // Return to scheduler
        _TaskSwap();
    }
    
//    template <typename T, typename T_Elm>
//    static void _Detach(T_Elm& elm) {
//        T& x = static_cast<T&>(elm);
//        T& l = *x.prev;
//        T& r = *x.next;
//        l.next = &r;
//        r.prev = &l;
//        x.reset();
//    }
//    
//    template <typename T, typename T_Elm>
//    static void _Attach(T& l, T_Elm& elm) {
//        T& x = static_cast<T&>(elm);
//        T& r = *l.next;
//        x.prev = &l;
//        x.next = &r;
//        l.next = &x;
//        r.prev = &x;
//    }
    
    template <typename T>
    static void Send(T& chan, const typename T::Type& val) {
        Send(chan, val, std::nullopt);
    }
    
    #warning TODO: consider case where TaskA and TaskB are waiting to send on a channel (they're both in chan._senders). TaskA is awoken but it's stopped before it executes. In this case TaskB needs to be awoken to send, right?
    #warning TODO: implement timeout
    // Buffered send
    template <typename T>
    static bool Send(T& chan, const typename T::Type& val, std::optional<Deadline> deadline) {
        IntState ints(false);
        
        // If the channel's full, or there's already a line waiting to send,
        // add ourself to the line.
        if (chan.full() || !chan._senders.empty()) {
            // Add ourself as a sender
            chan._senders.push(*_TaskCurr);
            _TaskSleep(deadline);
            _TaskCurr->listChannel().pop();
        }
        
        chan._q[chan._w] = val;
        chan._w++;
        if (chan._w == T::Cap) chan._w = 0;
        if (chan._w == chan._r) chan._full = true;
        
        // If there's a receiver, wake it
        if (!chan._receivers.empty()) {
            _TaskWake(*chan._receivers.next, _WakeReason::Event);
        }
        
        return true;
    }
    
    template <typename T>
    static typename T::Type Recv(T& chan) {
        return *Recv(chan, std::nullopt);
    }
    
    #warning TODO: implement timeout
    // Buffered receive
    template <typename T>
    static std::optional<typename T::Type> Recv(T& chan, std::optional<Deadline> deadline) {
        IntState ints(false);
        
        // If the channel's empty, or there's already a line waiting to receive,
        // add ourself to the line.
        if (chan.empty() || !chan._receivers.empty()) {
            // Add ourself as a receiver
            chan._receivers.push(*_TaskCurr);
            _TaskSleep(deadline);
            _TaskCurr->listChannel().pop();
        }
        
        const typename T::Type& val = chan._q[chan._r];
        chan._r++;
        if (chan._r == T::Cap) chan._r = 0;
        chan._full = false;
        
        // If there's a sender, wake it
        if (!chan._senders.empty()) {
            _TaskWake(*chan._senders.next, _WakeReason::Event);
        }
        return val;
    }
    
    static constexpr Ticks Us(uint16_t us) { return _TicksForUs(us); }
    static constexpr Ticks Ms(uint16_t ms) { return _TicksForUs(1000*(uint32_t)ms); }
    
    // Sleep(ticks): sleep current task for `ticks`
    static void Sleep(Ticks ticks) {
        // IntState:
        //   - ints must be disabled to prevent racing against Tick() ISR in accessing _ISR
        //   - int state must be restored upon return because scheduler clobbers it
        IntState ints(false);
        _TaskSleep(_ISR.CurrentTime+ticks);
    }
    
    // Tick(): notify scheduler that a tick has passed
    // Returns whether the scheduler needs to run
    static bool Tick() {
        bool woke = false;
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
        for (_ListRunSleep* i=_ListDeadline.next; i!=&_ListDeadline;) {
            _Task& task = static_cast<_Task&>(*i);
            if (task.wakeDeadline != _ISR.CurrentTime) break;
            // Update `i` before we potentially wake the task, because
            // waking the task disrupts the linked list.
            i = i->next;
            _TaskWake(task, _WakeReason::Deadline);
            woke = true;
        }
        _ISR.CurrentTime++;
        return woke;
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
    
    // _TaskSleep(): sleep current task either indefinitely, or until a deadline arrives
    static _WakeReason _TaskSleep(std::optional<Deadline> deadline=std::nullopt) {
        _TaskCurrSleepType = (deadline ? _SleepType::Deadline : _SleepType::Indefinite);
        _TaskCurr->wakeDeadline = deadline.value_or(0);
        // Return to scheduler
        _TaskSwap();
        return _TaskCurr->wakeReason;
    }
    
//    // _TaskSleep(deadline): sleep current task until `wake`.
//    // Returns true if the task awoke early because another task woke it.
//    static _WakeReason _TaskSleep(Deadline wake) {
//        _TaskCurrSleepType = _SleepType::Deadline;
//        _TaskCurr->wakeDeadline = wake;
//        // Return to scheduler
//        _TaskSwap();
//        return _TaskCurr->wakeReason;
//    }
    
    // _TaskWake: insert the given task into the running list
    template <typename T>
    static void _TaskWake(T& t, _WakeReason reason) {
        _Task& task = static_cast<_Task&>(t);
        // Remove task from run/sleep list
        task.listRunSleep().pop();
        // Insert task into the beginning of the runnable list (_ListRun)
        _ListRun.push(task);
        // Set the task's wake reason
        task.wakeReason = reason;
    }
    
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
                    _List<_ListRunSleep>{
                        .prev = (T_Idx==0 ?             &_ListRun : &_Tasks[T_Idx-1]),
                        .next = (T_Idx==_TaskCount-1 ?  &_ListRun : &_Tasks[T_Idx+1]),
                    }
                },
                .run        = T_Tasks::Run,
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
    
    enum class _SleepType : uint8_t {
        None,
        Indefinite,
        Deadline,
    };
    
    static inline _Task* _TaskCurr = nullptr;
    static inline _SleepType _TaskCurrSleepType = _SleepType::None;
    static inline _ListRunSleep _ListRun = {
        _List<_ListRunSleep>{
            .prev = &_Tasks[_TaskCount-1],
            .next = &_Tasks[0],
        },
    };
    static inline _ListRunSleep _ListDeadline;
    
    static volatile inline struct {
        Ticks CurrentTime = 0;
        Deadline WakeDeadline = 0;
        bool Wake = false;
    } _ISR;
#undef Assert
};

} // namespace Toastbox
