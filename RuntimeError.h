#pragma once
#include <sstream>

std::string _RuntimeErrorFmtMsg(const char* str) {
    char msg[256];
    int sr = snprintf(msg, sizeof(msg), "%s", str);
    if (sr<0 || (size_t)sr>=(sizeof(msg)-1)) throw std::runtime_error("failed to create RuntimeError");
    return msg;
}

template <typename ...Args>
std::string _RuntimeErrorFmtMsg(const char* fmt, Args ...args) {
    char msg[256];
    int sr = snprintf(msg, sizeof(msg), fmt, args...);
    if (sr<0 || (size_t)sr>=(sizeof(msg)-1)) throw std::runtime_error("failed to create RuntimeError");
    return msg;
}

template <typename ...Args>
std::runtime_error RuntimeError(const char* fmt, Args ...args) {
    return std::runtime_error(_RuntimeErrorFmtMsg(fmt, args...));
}
