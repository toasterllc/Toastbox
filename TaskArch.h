#pragma once

#if defined(TaskArchMSP430)

    #define TaskSaveRegs()                                                                      \
             if constexpr (sizeof(void*) == 2)  asm volatile("pushm   #7, r10" : : : );         \
        else if constexpr (sizeof(void*) == 4)  asm volatile("pushm.a #7, r10" : : : )

    #define TaskRestoreRegs()                                                                   \
             if constexpr (sizeof(void*) == 2)  asm volatile("popm   #7, r10" : : : );          \
        else if constexpr (sizeof(void*) == 4)  asm volatile("popm.a #7, r10" : : : )

    #define TaskSaveSP(dst)                                                                     \
             if constexpr (sizeof(void*) == 2)  asm volatile("mov  r1, %0" : "=m" (dst) : : );  \
        else if constexpr (sizeof(void*) == 4)  asm volatile("mova r1, %0" : "=m" (dst) : : )

    #define TaskRestoreSP(src)                                                                  \
             if constexpr (sizeof(void*) == 2)  asm volatile("mov  %0, r1" : : "m" (src) : );   \
        else if constexpr (sizeof(void*) == 4)  asm volatile("mova %0, r1" : : "m" (src) : )

    #define TaskRestorePC()                                                                     \
             if constexpr (sizeof(void*) == 2)  asm volatile("ret " : : : );                    \
        else if constexpr (sizeof(void*) == 4)  asm volatile("reta" : : : )

#elif defined(TaskArchARM32)

    #define TaskSaveRegs()      asm volatile("push {r4-r11}" : : : );
    #define TaskRestoreRegs()   asm volatile("pop {r4-r11" : : : );
    #define TaskSaveSP(dst)     asm volatile("" : "=m" (dst) : : );
    #define TaskRestoreSP(src)  asm volatile("" : : "m" (src) : );
    #define TaskRestorePC()     asm volatile("" : : : );

#else

#error Task: Unsupported architecture

#endif
