#pragma once
#include <sstream>

namespace Toastbox {

struct RuntimeError : std::runtime_error {
    static std::string _RuntimeErrorFmtMsg(const char* str) {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s", str);
        return msg;
    }

    template<typename ...Args>
    static std::string _RuntimeErrorFmtMsg(const char* fmt, Args&&... args) {
        char msg[512];
        snprintf(msg, sizeof(msg), fmt, std::forward<Args>(args)...);
        return msg;
    }
    
    template<typename ...Args>
    RuntimeError(const char* fmt, Args&&... args) :
    std::runtime_error(_RuntimeErrorFmtMsg(fmt, std::forward<Args>(args)...)) {}
};

} // namespace Toastbox
