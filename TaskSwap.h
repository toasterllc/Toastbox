#pragma once
#include <algorithm>

#if defined(TaskMSP430)

    template <bool T_Init=false, void T_InitFn(void)=nullptr>
    static void TaskSwap(void*& sp, void*& spSave) {
        if constexpr (sizeof(void*) == 2) {
            // MSP430 small memory model
            
            // Save regs
            asm volatile("pushm #7, r10" : : : );
            // Save SP to `spSave`
            asm volatile("mov SP, %0" : "=m" (spSave) : : );
            
            // Swap `sp` and `spSave`. It's crucial to do this between saving and
            // restoring, to ensure no registers get clobbered:
            //   - Do at beginning: potentially clobber registers before they're saved
            //   - Do at end: potentially clobber registers after they're restored
           
            std::swap(sp, spSave);
            
            // Restore SP from `spSave` (`sp` before swap)
            asm volatile("mov %0, SP" : : "m" (spSave) : );
            
            if constexpr (T_Init) {
                // Jump to `T_InitFn`
                asm volatile("mov %0, PC" : : "i" (T_InitFn) : );
            
            } else {
                // Restore regs
                asm volatile("popm #7, r10" : : : );
                // Restore PC
                asm volatile("ret" : : : );
            }
        
        } else {
            // MSP430 large memory model
            
            // Save regs
            asm volatile("pushm.a #7, r10" : : : );
            // Save SP to `spSave`
            asm volatile("mov.a SP, %0" : "=m" (spSave) : : );
            
            // Swap `sp` and `spSave`. It's crucial to do this between saving and
            // restoring, to ensure no registers get clobbered:
            //   - Do at beginning: potentially clobber registers before they're saved
            //   - Do at end: potentially clobber registers after they're restored
           
            std::swap(sp, spSave);
            
            // Restore SP from `spSave` (`sp` before swap)
            asm volatile("mov.a %0, SP" : : "m" (spSave) : );
            
            if constexpr (T_Init) {
                // Jump to `T_InitFn`
                asm volatile("mov.a %0, PC" : : "i" (T_InitFn) : );
            
            } else {
                // Restore regs
                asm volatile("popm.a #7, r10" : : : );
                // Restore PC
                asm volatile("ret.a" : : : );
            }
        }
    }

#elif defined(TaskARM32)



#else

#error Task: Unsupported architecture

#endif
