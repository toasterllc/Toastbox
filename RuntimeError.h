#pragma once
#include <sstream>

namespace Toastbox {

struct RuntimeError : std::runtime_error {
    // _RuntimeErrorFmtMsg() base case:
    // Necessary to silence "Format string is not a string literal (potentially insecure)"
    static std::string _RuntimeErrorFmtMsg(const char* str) {
        return str;
    }

    template<typename ...Args>
    static std::string _RuntimeErrorFmtMsg(const char* fmt, Args&&... args) {
        constexpr size_t Cap = 512;
        std::string msg;
        msg.reserve(Cap);
        snprintf(msg.data(), msg.capacity(), fmt, std::forward<Args>(args)...);
        return msg;
    }
    
    template<typename ...Args>
    RuntimeError(const char* fmt, Args&&... args) :
    std::runtime_error(_RuntimeErrorFmtMsg(fmt, std::forward<Args>(args)...)) {}
};

} // namespace Toastbox
