#pragma once
#include <sys/select.h>
#include "RuntimeError.h"

namespace Toastbox {

// Read()/Write(): simple wrappers around the read()/write() syscalls
// that handle EINTR, and throw an exception upon error

struct ReadWriteTimeout : std::exception {
    const char* what() const noexcept override {
        return "read/write timeout";
    }
};

template <bool T_Write>
inline bool _Select(int fd, const std::chrono::steady_clock::time_point& deadline) {
    using namespace std::chrono;
    int ir = 0;
    do {
        auto rem = deadline-steady_clock::now();
        auto sec = duration_cast<seconds>(rem);
        auto usec = duration_cast<microseconds>(rem-sec);
        
        struct timeval timeout = {
            .tv_sec = (int)sec.count(),
            .tv_usec = (int)usec.count(),
        };
        
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        if constexpr (!T_Write) ir = select(fd+1, &fds, nullptr, nullptr, &timeout);
        else                    ir = select(fd+1, nullptr, &fds, nullptr, &timeout);
    } while (ir==-1 && errno==EINTR);
    if (ir < 0) throw std::system_error(errno, std::generic_category());
    if (ir == 0) return false;
    return true;
}

inline size_t Read(int fd, void* data, size_t len, std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::time_point()) {
    uint8_t* d = (uint8_t*)data;
    size_t off = 0;
    
    while (off < len) {
        if (deadline.time_since_epoch().count()) {
            bool br = _Select<false>(fd, deadline);
            if (!br) return off;
        }
        
        ssize_t sr = 0;
        do sr = read(fd, d+off, len-off);
        while (sr==-1 && errno==EINTR);
        if (sr < 0) throw std::system_error(errno, std::generic_category());
        off += sr;
    }
    return off;
}

inline size_t Write(int fd, const void* data, size_t len, std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::time_point()) {
    const uint8_t* d = (const uint8_t*)data;
    size_t off = 0;
    
    while (off < len) {
        if (deadline.time_since_epoch().count()) {
            bool br = _Select<true>(fd, deadline);
            if (!br) return off;
        }
        
        ssize_t sr = 0;
        do sr = write(fd, d+off, len-off);
        while (sr==-1 && errno==EINTR);
        if (sr < 0) throw std::system_error(errno, std::generic_category());
        off += sr;
    }
    return off;
}

inline void Read(int fd, void* data, size_t len, std::chrono::milliseconds timeout) {
    Read(fd, data, len, std::chrono::steady_clock::now()+timeout);
}

inline void Write(int fd, const void* data, size_t len, std::chrono::milliseconds timeout) {
    Write(fd, data, len, std::chrono::steady_clock::now()+timeout);
}

} // namespace Toastbox
