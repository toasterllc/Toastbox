#pragma once
#include <algorithm>

#if defined(TaskArchMSP430)

#warning TODO: can we use `sp` instead of `r1`?

    template <bool T_Init=false, void T_InitFn(void)=nullptr>
    static void TaskArchSwap(void*& sp, void*& spSave) {
        if constexpr (sizeof(void*) == 2) {
            // MSP430 small memory model
            
            // Save regs
            asm volatile("pushm.w #7, r10" : : : );
            // Save SP to `spSave`
            asm volatile("mov.w r1, %0" : "=m" (spSave) : : );
            
            // Swap `sp` and `spSave`. It's crucial to do this between saving and
            // restoring, to ensure no registers get clobbered:
            //   - Do at beginning: potentially clobber registers before they're saved
            //   - Do at end: potentially clobber registers after they're restored
           
            std::swap(sp, spSave);
            
            // Restore SP from `spSave` (`sp` before swap)
            asm volatile("mov.w %0, r1" : : "m" (spSave) : );
            
            if constexpr (T_Init) {
                asm volatile("br %0" : : "i" (T_InitFn) : );
            
            } else {
                // Restore regs
                asm volatile("popm.w #7, r10" : : : );
                // Restore PC
                asm volatile("ret" : : : );
            }
        
        } else {
            // MSP430 large memory model
        
        }
    }


//    #define TaskArchSwap(sp, spSave, initFn)                                                \
//        if constexpr (sizeof(void*) == 2) {                                                 \
//            /* MSP430 small memory model */                                                 \
//                                                                                            \
//            /* Save regs */                                                                 \
//            asm volatile("pushm.w #7, r10" : : : );                                         \
//            /* Save SP to `spSave` */                                                       \
//            asm volatile("mov.w r1, %0" : "=m" (spSave) : : );                              \
//                                                                                            \
//            /* Swap `sp` and `spSave`. It's crucial to do this between saving and           \
//               restoring, to ensure no registers get clobbered:                             \
//                 - Do at beginning: potentially clobber registers before they're saved      \
//                 - Do at end: potentially clobber registers after they're restored          \
//            */                                                                              \
//            std::swap((sp), (spSave));                                                      \
//                                                                                            \
//            /* Restore SP from `spSave` (`sp` before swap) */                               \
//            asm volatile("mov.w %0, r1" : : "m" (spSave) : );                               \
//                                                                                            \
//            if constexpr (initFn) {                                                         \
//                asm volatile("br %0" : : "i" (_TaskSwap) : );                               \
//                                                                                            \
//            } else {                                                                        \
//                /* Restore regs */                                                          \
//                asm volatile("popm.w #7, r10" : : : );                                      \
//                /* Restore PC */                                                            \
//                asm volatile("ret" : : : );                                                 \
//            }                                                                               \
//                                                                                            \
//        } else {                                                                            \
//            /* MSP430 large memory model */                                                 \
//                                                                                            \
//        }

#elif defined(TaskArchARM32)



#else

#error Task: Unsupported architecture

#endif
