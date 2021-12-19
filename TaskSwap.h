#pragma once
#include <algorithm>

template <bool T_Init=false, void T_InitFn(void)=nullptr>
static void TaskSwap(void*& sp, void*& spSave) {
    
    // (1) Push callee-saved regs onto stack
    // (2) spSave = SP
    // (3) std::swap(sp, spSave)
    //
    //     It's crucial to perform this swap at this point (between saving and
    //     restoring) to ensure no registers get clobbered:
    //       - Do at beginning: potentially clobber registers before they're saved
    //       - Do at end: potentially clobber registers after they're restored
    //
    // (4) SP = spSave
    //
    // if T_Init:
    //   (5) Jump to `T_InitFn`
    //
    // else
    //   (6) Pop callee-saved registers from stack
    //   (7) Restore PC
    
#if defined(TaskMSP430)
    
    if constexpr (sizeof(void*) == 2) {
        // ## Architecture = MSP430, small memory model
        asm volatile("pushm #7, r10" : : : );                   // (1)
        asm volatile("mov SP, %0" : "=m" (spSave) : : );        // (2)
        std::swap(sp, spSave);                                  // (3)
        asm volatile("mov %0, SP" : : "m" (spSave) : );         // (4)
        if constexpr (T_Init) {
            asm volatile("mov %0, PC" : : "i" (T_InitFn) : );   // (5)
        } else {
            asm volatile("popm #7, r10" : : : );                // (6)
            asm volatile("ret" : : : );                         // (7)
        }
    
    } else {
        // ## Architecture = MSP430, large memory model
        asm volatile("pushm.a #7, r10" : : : );                 // (1)
        asm volatile("mov.a SP, %0" : "=m" (spSave) : : );      // (2)
        std::swap(sp, spSave);                                  // (3)
        asm volatile("mov.a %0, SP" : : "m" (spSave) : );       // (4)
        if constexpr (T_Init) {
            asm volatile("mov.a %0, PC" : : "i" (T_InitFn) : ); // (5)
        } else {
            asm volatile("popm.a #7, r10" : : : );              // (6)
            asm volatile("ret.a" : : : );                       // (7)
        }
    }
    
#elif defined(TaskARM32)
    
    // ## Architecture = ARM32, large memory model
    asm volatile("pushm.a #7, r10" : : : );                 // (1)
    asm volatile("mov.a SP, %0" : "=m" (spSave) : : );      // (2)
    std::swap(sp, spSave);                                  // (3)
    asm volatile("mov.a %0, SP" : : "m" (spSave) : );       // (4)
    if constexpr (T_Init) {
        asm volatile("mov.a %0, PC" : : "i" (T_InitFn) : ); // (5)
    } else {
        asm volatile("popm.a #7, r10" : : : );              // (6)
        asm volatile("ret.a" : : : );                       // (7)
    }
    
#else
    
    #error Task: Unsupported architecture
    
#endif
    
}
