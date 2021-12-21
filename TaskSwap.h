#pragma once

// TaskSwap(): architecture-specific implementation that swaps the current
// task with a different task.
//
// (1) Push callee-saved regs onto stack, including $PC if needed
// (2) tmp = $SP
// (3) $SP = `sp`
// (4) `sp` = tmp
// if `initFn`:
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
