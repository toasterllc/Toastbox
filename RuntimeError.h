#pragma once
#include <sstream>

namespace Toastbox {

inline std::string _RuntimeErrorFmtMsg(const char* str) {
    char msg[512];
    snprintf(msg, sizeof(msg), "%s", str);
    return msg;
}

template<typename ...Args>
inline std::string _RuntimeErrorFmtMsg(const char* fmt, Args&&... args) {
    char msg[512];
    snprintf(msg, sizeof(msg), fmt, std::forward<Args>(args)...);
    return msg;
}

template<typename ...Args>
inline std::runtime_error RuntimeError(const char* fmt, Args&&... args) {
    return std::runtime_error(_RuntimeErrorFmtMsg(fmt, std::forward<Args>(args)...));
}

} // namespace Toastbox
