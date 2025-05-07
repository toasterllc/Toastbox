#pragma once
#include <sstream>
#include "RuntimeError.h"

namespace Toastbox {

struct KernError : RuntimeError {
    template<typename ...Args>
    KernError(kern_return_t kr, const char* fmt, Args&&... args) :
    RuntimeError("%s: %s (0x%x)", _RuntimeErrorFmtMsg(fmt, std::forward<Args>(args)...).c_str(),
    mach_error_string(kr), kr), kr(kr) {}
    
    kern_return_t kr = KERN_SUCCESS;
};

} // namespace Toastbox
