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
        std::string msg(Cap-1, '\0'); // string capacity = (Cap-1)+1 = 512
        int ir = snprintf(msg.data(), Cap, fmt, std::forward<Args>(args)...);
        assert(ir >= 0);
        msg.resize(ir);
        return msg;
    }
    
    template<typename ...Args>
    RuntimeError(const char* fmt, Args&&... args) :
    std::runtime_error(_RuntimeErrorFmtMsg(fmt, std::forward<Args>(args)...)) {}
};

} // namespace Toastbox
