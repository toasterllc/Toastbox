#pragma once

#if defined(TaskArchMSP430)

    #define TaskRegsSave()                                                                      \
             if constexpr (sizeof(void*) == 2)  asm volatile("pushm   #7, r10" : : : );         \
        else if constexpr (sizeof(void*) == 4)  asm volatile("pushm.a #7, r10" : : : )

    #define TaskRegsRestore()                                                                   \
             if constexpr (sizeof(void*) == 2)  asm volatile("popm   #7, r10" : : : );          \
        else if constexpr (sizeof(void*) == 4)  asm volatile("popm.a #7, r10" : : : )

    #define TaskSPSave(dst)                                                                     \
             if constexpr (sizeof(void*) == 2)  asm volatile("mov  r1, %0" : "=m" (dst) : : );  \
        else if constexpr (sizeof(void*) == 4)  asm volatile("mova r1, %0" : "=m" (dst) : : )

    #define TaskSPRestore(src)                                                                  \
             if constexpr (sizeof(void*) == 2)  asm volatile("mov  %0, r1" : : "m" (src) : );   \
        else if constexpr (sizeof(void*) == 4)  asm volatile("mova %0, r1" : : "m" (src) : )

    #define TaskPCRestore()                                                                     \
             if constexpr (sizeof(void*) == 2)  asm volatile("ret " : : : );                    \
        else if constexpr (sizeof(void*) == 4)  asm volatile("reta" : : : )

#elif defined(TaskArchARM32)

    #define TaskRegsSave()      asm volatile("" : : : );
    #define TaskRegsRestore()   asm volatile("" : : : );
    #define TaskSPSave(dst)     asm volatile("" : : : );
    #define TaskSPRestore(src)  asm volatile("" : : : );
    #define TaskPCRestore()     asm volatile("" : : : );

#else

#error Task: Unsupported architecture

#endif
