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
        T* prev = nullptr;
        T* next = nullptr;
        
        template <typename T_Elm>
        T& push(T_Elm& elm) {
            // Ensure that the element isn't a part of any list before we attach it
            T& x = static_cast<T&>(elm);
            x._detach();
            
            T& l = static_cast<T&>(*this);
            T*const r = l.next;
            x.prev = &l;
            x.next = r;
            l.next = &x;
            if (r) r->prev = &x;
            return x;
        }
        
        void pop() {
            _detach();
            *this = {};
        }
        
        // other(): returns another element in the list, or nullptr if there is no other element.
        // Never returns the root element.
        T* other() {
            if (next) return next;
            if (prev && prev->prev) return prev;
            return nullptr;
        }
        
        void _detach() {
            T*const l = prev;
            T*const r = next;
            if (l) l->next = r;
            if (r) r->prev = l;
        }
    };
    
    struct _ListRunType : _List<_ListRunType> {};
    struct _ListDeadlineType : _List<_ListDeadlineType> {};
    struct _ListChannelType : _List<_ListChannelType> {};
    
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
    
public:
    using Ticks     = unsigned int;
    using Deadline  = Ticks;
    
    // Signal: empty type that can be used with channels; useful when
    // signalling is necessary but data transfer is not
    using Signal = struct{ int _[0]; };
    static_assert(sizeof(Signal) == 0);
    
    // MARK: - Channel
    template <typename T_Type, size_t T_Cap=1>
    class Channel {
    public:
        using Type = T_Type;
        static constexpr size_t Cap = T_Cap;
        
        Channel() {} // Constructor definition necessary to workaround compiler error:
                     // "default member initializer for XXX required before
                     // the end of its enclosing class"
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
    
    // MARK: - BlockStyle
    enum class BlockStyle : uint8_t {
        Blocking,
        Nonblocking,
        Duration,
        Deadline,
    };
    
    // Run(): run the tasks indefinitely
    [[noreturn]]
    static void Run() {
        // Prepare every task
        for (_Task& task : _Tasks) {
            // Initialize the task's stack guard
            if constexpr (_StackGuardEnabled) _StackGuardInit(*task.stackGuard);
            _TaskStackInit(task);
        }
        
        // Initialize the interrupt stack guard
        if constexpr (_InterruptStackGuardEnabled) _StackGuardInit(_InterruptStackGuard);
        
        // junk: dummy task that _TaskSwap saves the current stack pointer to,
        // which is thrown away.
        _Task junk = { .stackGuard = _Tasks[0].stackGuard };
        _TaskCurr = &junk;
        _TaskSwap(false);
        for (;;);
    }
    
    // Yield(): yield current task to the scheduler
    static void Yield() {
        IntState ints(false);
        _TaskSwap(false);
    }
    
    #warning TODO: consider case where TaskA and TaskB are waiting to send on a channel (they're both in chan._senders). TaskA is awoken but it's stopped before it executes. In this case TaskB needs to be awoken to send, right?
    // Buffered send
    template <typename T>
    static bool Send(T& chan, const typename T::Type& val,
        BlockStyle block=BlockStyle::Blocking, Ticks ticks=0) {
        
        IntState ints(false);
        
        // If we're non-blocking and the channel is full, return immediately
        if (block==BlockStyle::Nonblocking && chan.full()) {
            return false;
        }
        
        // Wait until the channel isn't full
        if (chan.full()) {
            const std::optional<Deadline> deadline = _Deadline(block, ticks);
            _ListRemoverChannel cleanupChannel = chan._senders.push(*_TaskCurr);
            _ListRemoverDeadline cleanupDeadline;
            if (deadline) cleanupDeadline = _ListDeadlineInsert(*_TaskCurr, *deadline);
            
            for (;;) {
                _TaskSwap(true);
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
        if (chan._receivers.next) {
            // Insert task into the beginning of the runnable list (_ListRun)
            _ListRunInsert(*chan._receivers.next);
        }
        
        return true;
    }
    
    // Buffered receive
    template <typename T>
    static std::optional<typename T::Type> Recv(T& chan,
        BlockStyle block=BlockStyle::Blocking, Ticks ticks=0) {
        
        IntState ints(false);
        
        // If we're non-blocking and the channel is empty, return immediately
        if (block==BlockStyle::Nonblocking && chan.empty()) {
            return std::nullopt;
        }
        
        if (chan.empty()) {
            const std::optional<Deadline> deadline = _Deadline(block, ticks);
            _ListRemoverChannel cleanupChannel = chan._receivers.push(*_TaskCurr);
            _ListRemoverDeadline cleanupDeadline;
            if (deadline) cleanupDeadline = _ListDeadlineInsert(*_TaskCurr, *deadline);
            
            for (;;) {
                _TaskSwap(true);
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
        if (chan._senders.next) {
            // Insert task into the beginning of the runnable list (_ListRun)
            _ListRunInsert(*chan._senders.next);
        }
        return val;
    }
    
    template <typename T>
    static void Clear(T& chan) {
        IntState ints(false);
        
        chan._r = 0;
        chan._w = 0;
        chan._full = false;
        
        // If there's a sender, wake it
        if (chan._senders.next) {
            // Insert task into the beginning of the runnable list (_ListRun)
            _ListRunInsert(*chan._senders.next);
        }
    }
    
    static constexpr Ticks Us(uint16_t us) { return _TicksForUs(us); }
    static constexpr Ticks Ms(uint16_t ms) { return _TicksForUs(1000*(uint32_t)ms); }
    
    // Sleep(ticks): sleep current task for `ticks`
    static void Sleep(Ticks ticks) {
        // Ints must be disabled to prevent racing against Tick()
        // ISR in accessing _ISR.CurrentTime
        IntState ints(false);
        _ListDeadlineInsert(*_TaskCurr, _ISR.CurrentTime+ticks);
        _TaskSwap(true);
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
        for (_ListDeadlineType* i=_ListDeadline.next; i;) {
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
    
    
    // Restart<T_Task>(): restarts `T_Task` in its Run() function
    template <typename T_Task>
    static void Restart() {
        constexpr _Task& task = _GetTask<T_Task>();
        _TaskInit(task, true);
    }
    
    // Stop<T_Task>(): stops `T_Task`
    template <typename T_Task>
    static void Stop() {
        constexpr _Task& task = _GetTask<T_Task>();
        _TaskInit(task, false);
    }
    
private:
    // MARK: - Types
    static constexpr uintptr_t _StackGuardMagicNumber = (uintptr_t)0xCAFEBABEBABECAFE;
    using _StackGuard = uintptr_t[T_StackGuardCount];
    
    // We have to use __attribute__((noreturn)) instead of [[noreturn]]
    // because [[noreturn]] can't be applied to types.
    using _TaskFn = void(*)();
    
    struct _Task : _ListRunType, _ListDeadlineType, _ListChannelType {
        __attribute__((noreturn)) _TaskFn run = nullptr;
        void* spInit = nullptr;
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
    
    static void _TaskInit(_Task& task, bool run) {
        IntState ints(false);
        
        // Remove task from deadline list
        task.listDeadline().pop();
        
        // Remove task from channel list
        // Before we do so, we need to find another task to wake from the same channel list.
        // Waking this other task is necessary because it's possible that `task` was about to run
        // because it was awoken to send/receive on a channel, but is now being reset before it
        // got the chance to run. To properly handle that case, we need to wake the subsequent
        // task in the channel list, otherwise there'd be a task waiting to send/receive even
        // though sending/receiving was possible.
        _Task*const channelWakeTask = static_cast<_Task*>(task.listChannel().other());
        task.listChannel().pop();
        if (channelWakeTask) _ListRunInsert(*channelWakeTask);
        
        // Re-init the task's stack
        _TaskStackInit(task);
        
        // Insert or remove the task from the runnable list, depending on `run`
        if (run) _ListRunInsert(task);
        else task.listRun().pop();
    }
    
    // _TaskStackInit(): init the task's stack
    // Ints must be disabled
    static void _TaskStackInit(_Task& task) {
        const size_t extra = (_SchedulerStackSaveRegCount+1) % _SchedulerStackAlign;
        void**& sp = *((void***)&task.sp);
        // Reset stack pointer
        sp = (void**)task.spInit;
        // Push extra slots to ensure `_SchedulerStackAlign` alignment
        sp -= extra;
        // Push initial return address == task.run address == Task::Run
        sp--;
        *sp = (void*)_TaskRun;
        // Push registers that __TaskSwap() expects to be saved on the stack.
        // We don't care about what values the registers contain since they're not actually used.
        sp -= _SchedulerStackSaveRegCount;
    }
    
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
            if (!i) break;
            const _Task& t = static_cast<_Task&>(*i);
            const Ticks d = *t.wakeDeadline - _ISR.CurrentTime;
            // Use >= instead of > so that we attach the task at the earliest
            // available slot, to minimize our computation.
            if (delta >= d) break;
            insert = i;
        }
        return insert->push(task);
    }
    
    [[noreturn]]
    static void _TaskRun() {
        // Enable interrupts before entering the task for the first time
        IntState::Set(true);
        _TaskCurr->run();
    }
    
    // _TaskSwap(): saves _TaskCurr and restores _TaskNext
    // Ints must be disabled
    static void _TaskSwap(bool sleep) {
        // Check stack guards
        if constexpr (_StackGuardEnabled) _StackGuardCheck(*_TaskCurr->stackGuard);
        if constexpr (_InterruptStackGuardEnabled) _StackGuardCheck(_InterruptStackGuard);
        
        // Update _TaskNext to be the next task
        _ListRunType& listRun = _TaskCurr->listRun();
        _TaskNext = static_cast<_Task*>(listRun.next);
        
        // If the current task is going to sleep, remove it from _ListRun
        if (sleep) listRun.pop();
        
        // Sleep while there are no tasks to run
        while (!_ListRun.next) T_Sleep();
        
        // If _TaskNext reached the end of the list, restart it from the beginning
        if (!_TaskNext) _TaskNext = static_cast<_Task*>(_ListRun.next);
        
        std::swap(_TaskCurr, _TaskNext);
        __TaskSwap();
    }
    
    static std::optional<Deadline> _Deadline(BlockStyle block, Ticks ticks) {
        switch (block) {
        case BlockStyle::Duration:  return _ISR.CurrentTime+ticks;
        case BlockStyle::Deadline:  return ticks;
        default:                    return std::nullopt;
        }
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
                        .next = (T_Idx==_TaskCount-1 ?  nullptr   : &_Tasks[T_Idx+1]),
                    }
                },
                .run        = (decltype(_Task::run))T_Tasks::Run,
                .spInit     = T_Tasks::Stack + sizeof(T_Tasks::Stack),
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
            .next = &_Tasks[0],
        },
    };
    static inline _ListDeadlineType _ListDeadline;
    
    static volatile inline struct {
        Ticks CurrentTime = 0;
    } _ISR;
};

} // namespace Toastbox
