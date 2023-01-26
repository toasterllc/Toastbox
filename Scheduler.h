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
// (2) Save $SP (stack pointer) into `spSave` (macro argument)
// (3) Restore $SP (stack pointer) from `spRestore` (macro argument)
// (4) Pop callee-saved registers from stack
// (5) Return to caller

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
#define _SchedulerTaskSwap(spSave, spRestore)                                           \
                                                                                        \
    /* ## Architecture = AMD64 */                                                       \
    asm volatile("push %%rbx" : : : );                                  /* (1) */       \
    asm volatile("push %%rbp" : : : );                                  /* (1) */       \
    asm volatile("push %%r12" : : : );                                  /* (1) */       \
    asm volatile("push %%r13" : : : );                                  /* (1) */       \
    asm volatile("push %%r14" : : : );                                  /* (1) */       \
    asm volatile("push %%r15" : : : );                                  /* (1) */       \
    asm volatile("mov %%rsp, %0" : "=m" (spSave) : : );                 /* (2) */       \
    asm volatile("mov %0, %%rsp" : : "m" (spRestore) : );               /* (3) */       \
    asm volatile("pop %%r15" : : : );                                   /* (4) */       \
    asm volatile("pop %%r14" : : : );                                   /* (4) */       \
    asm volatile("pop %%r13" : : : );                                   /* (4) */       \
    asm volatile("pop %%r12" : : : );                                   /* (4) */       \
    asm volatile("pop %%rbp" : : : );                                   /* (4) */       \
    asm volatile("pop %%rbx" : : : );                                   /* (4) */       \
    asm volatile("ret" : : : );                                         /* (5) */       \
    
#else
    
    #error Task: Unsupported architecture
    
#endif

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
    template <typename T>
    struct _List {
        T* prev = static_cast<T*>(this);
        T* next = static_cast<T*>(this);
        
        bool empty() const { return next==this; }
        
        template <typename T_Elm>
        T& push(T_Elm& elm) {
            // Ensure that the element isn't a part of any list before we attach it
            T& x = static_cast<T&>(elm);
            x._pop();
            
            T& l = static_cast<T&>(*this);
            T& r = *l.next;
            x.prev = &l;
            x.next = &r;
            l.next = &x;
            r.prev = &x;
            return x;
        }
        
        void pop() {
            _pop();
            prev = static_cast<T*>(this);
            next = static_cast<T*>(this);
        }
        
        void _pop() {
            T& l = *prev;
            T& r = *next;
            l.next = &r;
            r.prev = &l;
        }
    };
    
    struct _ListRunType : _List<_ListRunType> {};
    struct _ListDeadlineType : _List<_ListDeadlineType> {};
    struct _ListChannelType : _List<_ListChannelType> {};
    
public:
    using Ticks     = unsigned int;
    using Deadline  = Ticks;
    
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
        _ListChannelType _senders;
        _ListChannelType _receivers;
    };
    
    // Run(): run the tasks indefinitely
    [[noreturn]]
    static void Run() {
        // Prepare every task
        for (_Task& task : _Tasks) {
            // Initialize the task's stack guard
            if constexpr (_StackGuardEnabled) _StackGuardInit(*task.stackGuard);
            
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
        
        // Initialize the interrupt stack guard
        if constexpr (_InterruptStackGuardEnabled) _StackGuardInit(_InterruptStackGuard);
        
        _Task junk = { .stackGuard = _Tasks[0].stackGuard };
        _TaskCurr = &junk;
        _TaskNext = &_Tasks[0];
        _TaskSwap();
        for (;;);
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        IntState ints(false);
        _TaskYield();
    }
    
    enum class BlockStyle : uint8_t {
        Blocking,
        Nonblocking,
        Deadline,
    };
    
    #warning TODO: consider case where TaskA and TaskB are waiting to send on a channel (they're both in chan._senders). TaskA is awoken but it's stopped before it executes. In this case TaskB needs to be awoken to send, right?
    // Buffered send
    template <typename T>
    static bool Send(T& chan, const typename T::Type& val,
        BlockStyle block=BlockStyle::Blocking, Deadline deadline=0) {
        
        IntState ints(false);
        
        // If we're non-blocking and the channel is full, return immediately
        if (block==BlockStyle::Nonblocking && chan.full()) {
            return false;
        }
        
        // Wait until the channel isn't full
        if (chan.full()) {
            _ListRemoverChannel cleanupChannel = chan._senders.push(*_TaskCurr);
            _ListRemoverDeadline cleanupDeadline;
            if (block == BlockStyle::Deadline) cleanupDeadline = _ListDeadlineInsert(*_TaskCurr, deadline);
            
            for (;;) {
                _TaskSleep();
                // Check if channel can send
                if (!chan.full()) break;
                // Check for timeout
                else if (!_TaskCurr->wakeDeadline) return false;
            }
        }
        
        // Clear to send!
        chan._q[chan._w] = val;
        chan._w++;
        if (chan._w == T::Cap) chan._w = 0;
        if (chan._w == chan._r) chan._full = true;
        
        // If there's a receiver, wake it
        if (!chan._receivers.empty()) {
            // Insert task into the beginning of the runnable list (_ListRun)
            _ListRunInsert(*chan._receivers.next);
        }
        
        return true;
    }
    
    // Buffered receive
    template <typename T>
    static std::optional<typename T::Type> Recv(T& chan,
        BlockStyle block=BlockStyle::Blocking, Deadline deadline=0) {
        
        IntState ints(false);
        
        // If we're non-blocking and the channel is empty, return immediately
        if (block==BlockStyle::Nonblocking && chan.empty()) {
            return std::nullopt;
        }
        
        if (chan.empty()) {
            _ListRemoverChannel cleanupChannel = chan._receivers.push(*_TaskCurr);
            _ListRemoverDeadline cleanupDeadline;
            if (block == BlockStyle::Deadline) cleanupDeadline = _ListDeadlineInsert(*_TaskCurr, deadline);
            
            for (;;) {
                _TaskSleep();
                // Check if channel can receive
                if (!chan.empty()) break;
                // Check for timeout
                else if (!_TaskCurr->wakeDeadline) return std::nullopt;
            }
        }
        
        const typename T::Type& val = chan._q[chan._r];
        chan._r++;
        if (chan._r == T::Cap) chan._r = 0;
        chan._full = false;
        
        // If there's a sender, wake it
        if (!chan._senders.empty()) {
            // Insert task into the beginning of the runnable list (_ListRun)
            _ListRunInsert(*chan._senders.next);
        }
        return val;
    }
    
    static constexpr Ticks Us(uint16_t us) { return _TicksForUs(us); }
    static constexpr Ticks Ms(uint16_t ms) { return _TicksForUs(1000*(uint32_t)ms); }
    
    // Sleep(ticks): sleep current task for `ticks`
    static void Sleep(Ticks ticks) {
        // Ints must be disabled to prevent racing against Tick()
        // ISR in accessing _ISR.CurrentTime
        IntState ints(false);
        _ListDeadlineInsert(*_TaskCurr, _ISR.CurrentTime+ticks);
        _TaskSleep();
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
        for (auto* i=_ListDeadline.next; i!=&_ListDeadline;) {
            _Task& task = static_cast<_Task&>(*i);
            if (*task.wakeDeadline != _ISR.CurrentTime) break;
            // Update `i` before we potentially wake the task, because
            // waking the task disrupts the linked list.
            i = i->next;
            // Clear deadline to signal to the task that the deadline has passed
            task.wakeDeadline = std::nullopt;
            // Remove task from the deadline list
            task.listDeadline().pop();
            // Add task to runnable list
            _ListRun.push(task);
            woke = true;
        }
        _ISR.CurrentTime++;
        return woke;
    }
    
    static Ticks CurrentTime() {
        // Ints must be disabled to prevent racing against Tick()
        // ISR in accessing _ISR.CurrentTime
        IntState ints(false);
        return _ISR.CurrentTime;
    }
    
private:
    // MARK: - Types
    static constexpr uintptr_t _StackGuardMagicNumber = (uintptr_t)0xCAFEBABEBABECAFE;
    using _StackGuard = uintptr_t[T_StackGuardCount];
    
    using _TaskFn = void(*)();
    
    struct _Task : _ListRunType, _ListDeadlineType, _ListChannelType {
        _TaskFn run = nullptr;
        void* sp = nullptr;
        std::optional<Deadline> wakeDeadline;
        _StackGuard* stackGuard = nullptr;
        
        auto& listRun() { return static_cast<_ListRunType&>(*this); }
        auto& listDeadline() { return static_cast<_ListDeadlineType&>(*this); }
        auto& listChannel() { return static_cast<_ListChannelType&>(*this); }
    };
    
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
    
    // _ListRemover: removes the element from the linked list upon destruction
    template <typename T>
    class _ListRemover {
    public:
        _ListRemover() {}
        _ListRemover(T& x) : _x(&x) {}
        _ListRemover(const _ListRemover& x)             = delete;
        _ListRemover& operator=(const _ListRemover& x)  = delete;
        _ListRemover(_ListRemover&& x)                  { swap(x); }
        _ListRemover& operator=(_ListRemover&& x)       { swap(x); return *this; }
        ~_ListRemover() { if (_x) _x->pop(); }
        void swap(_ListRemover& x) { std::swap(_x, x._x); }
    private:
        T* _x = nullptr;
    };
    
    using _ListRemoverDeadline = _ListRemover<_ListDeadlineType>;
    using _ListRemoverChannel = _ListRemover<_ListChannelType>;
    
    // _ListRunInsert(): insert task into runnable list
    // Ints must be disabled
    template <typename T>
    static void _ListRunInsert(T& t) {
        _Task& task = static_cast<_Task&>(t);
        // Insert task into the beginning of the runnable list (_ListRun)
        _ListRun.push(task);
    }
    
    // _ListDeadlineInsert(): insert task into deadline list, so that it's awoken
    // when the deadline arrives.
    // Ints must be disabled
    static _ListDeadlineType& _ListDeadlineInsert(_Task& task, Deadline deadline) {
        task.wakeDeadline = deadline;
        
        // Insert `task` into the appropriate point in `_ListDeadline`
        // (depending on its wakeDeadline)
        const Ticks delta = deadline-_ISR.CurrentTime;
        _ListDeadlineType* insert = &_ListDeadline;
        for (;;) {
            _ListDeadlineType*const i = insert->next;
            // If we're at the end of the list, we're done
            if (i == &_ListDeadline) break;
            const _Task& t = static_cast<_Task&>(*i);
            const Ticks d = *t.wakeDeadline - _ISR.CurrentTime;
            // Use >= instead of > so that we attach the task at the earliest
            // available slot, to minimize our computation.
            if (delta >= d) break;
            insert = i;
        }
        return insert->push(task);
    }
    
    // _TaskSleep(): remove the current task from the runnable list of tasks,
    // and switch to the next task.
    // Ints must be disabled
    static void _TaskSleep() {
        // Get _TaskCurr's entry in _ListRun
        _ListRunType& listRun = _TaskCurr->listRun();
        // Update _TaskNext to be the task after _TaskCurr, before removing
        // _TaskCurr from _ListRun
        _TaskNext = static_cast<_Task*>(listRun.next);
        // Remove _TaskCurr from _ListRun
        listRun.pop();
        // Return to scheduler
        _TaskSwap();
    }
    
    // _TaskYield(): switch to the next task without changing the runnability
    // of the current task.
    // Ints must be disabled
    static void _TaskYield() {
        // Update _TaskNext to be the task after _TaskCurr
        _TaskNext = static_cast<_Task*>(_TaskCurr->listRun().next);
        // Return to scheduler
        _TaskSwap();
    }
    
    // _TaskSwap(): saves _TaskCurr and restores _TaskNext
    // Ints must be disabled
    static void _TaskSwap() {
        // Check stack guards
        if constexpr (_StackGuardEnabled) _StackGuardCheck(*_TaskCurr->stackGuard);
        if constexpr (_InterruptStackGuardEnabled) _StackGuardCheck(_InterruptStackGuard);
        
        // Sleep until we have a task to run
        while (_ListRun.empty()) T_Sleep();
        
        if (_TaskNext == &_ListRun) {
            _TaskNext = static_cast<_Task*>(_ListRun.next);
        }
        
        std::swap(_TaskCurr, _TaskNext);
        IntState ints(true); // Save/restore interrupt state
        __TaskSwap();
    }
    
    // __TaskSwap(): saves current stack pointer into _TaskNext->sp and
    // restores the stack pointer to _TaskCurr->sp (note that tasks must
    // be swapped before calling)
    [[gnu::noinline, gnu::naked]] // Don't inline: PC must be pushed onto the stack when called
    static void __TaskSwap() {
        _SchedulerTaskSwap(_TaskNext->sp, _TaskCurr->sp);
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
                _ListRunType{
                    _List<_ListRunType>{
                        .prev = (T_Idx==0 ?             &_ListRun : &_Tasks[T_Idx-1]),
                        .next = (T_Idx==_TaskCount-1 ?  &_ListRun : &_Tasks[T_Idx+1]),
                    }
                },
                .run        = T_Tasks::Run,
                .sp         = T_Tasks::Stack + sizeof(T_Tasks::Stack),
                .stackGuard = (_StackGuard*)T_Tasks::Stack,
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
    
    static inline _Task* _TaskCurr = nullptr;
    static inline _Task* _TaskNext = nullptr;
    static inline _ListRunType _ListRun = {
        _List<_ListRunType>{
            .prev = &_Tasks[_TaskCount-1],
            .next = &_Tasks[0],
        },
    };
    static inline _ListDeadlineType _ListDeadline;
    
    static volatile inline struct {
        Ticks CurrentTime = 0;
    } _ISR;
};

} // namespace Toastbox
