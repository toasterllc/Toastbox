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

inline int _FDMax(const int* fds, size_t fdsLen) {
    int r = -1;
    for (size_t i=0; i<fdsLen; i++) r = std::max(r, fds[i]);
    return r;
}

inline fd_set _FDSet(const int* fds, size_t fdsLen) {
    fd_set r;
    FD_ZERO(&r);
    for (size_t i=0; i<fdsLen; i++) FD_SET(fds[i], &r);
    return r;
}

inline bool Select(int* rfds, size_t rfdsLen, int* wfds, size_t wfdsLen,
std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::time_point()) {
    
    using namespace std::chrono;
    
    // Determine fdMax (the largest supplied fd)
    const int fdMax = std::max(_FDMax(rfds, rfdsLen), _FDMax(wfds, wfdsLen));
    const fd_set rfdconst = _FDSet(rfds, rfdsLen);
    const fd_set wfdconst = _FDSet(wfds, wfdsLen);
    
    fd_set rfd;
    fd_set wfd;
    int ir = 0;
    do {
        // Prepare our timeout if a deadline was specified
        struct timeval timeout;
        struct timeval* timeoutp = nullptr;
        if (deadline.time_since_epoch().count()) {
            auto rem = deadline-steady_clock::now();
            auto sec = duration_cast<seconds>(rem);
            auto usec = duration_cast<microseconds>(rem-sec);
            
            timeout = {
                .tv_sec = std::max(0, (int)sec.count()),
                .tv_usec = std::max(0, (int)usec.count()),
            };
            timeoutp = &timeout;
        }
        
        rfd = rfdconst;
        wfd = wfdconst;
        ir = select(fdMax+1, &rfd, &wfd, nullptr, timeoutp);
    
    } while (ir==-1 && errno==EINTR);
    if (ir < 0) throw std::system_error(errno, std::generic_category());
    if (ir == 0) return false; // Timeout
    
    // Clear the fds in rfds that aren't ready for reading
    for (size_t i=0; i<rfdsLen; i++) {
        const int fd = rfds[i];
        if (!FD_ISSET(fd, &rfd)) rfds[i] = -1;
    }
    
    // Clear the fds in wfds that aren't ready for writing
    for (size_t i=0; i<wfdsLen; i++) {
        const int fd = wfds[i];
        if (!FD_ISSET(fd, &wfd)) wfds[i] = -1;
    }
    
    return true;
}


//template<bool T_Write>
//inline bool _Select(int fd, const std::chrono::steady_clock::time_point& deadline) {
//    using namespace std::chrono;
//    int ir = 0;
//    do {
//        auto rem = deadline-steady_clock::now();
//        auto sec = duration_cast<seconds>(rem);
//        auto usec = duration_cast<microseconds>(rem-sec);
//        
//        struct timeval timeout = {
//            .tv_sec = (int)sec.count(),
//            .tv_usec = (int)usec.count(),
//        };
//        
//        fd_set fds;
//        FD_ZERO(&fds);
//        FD_SET(fd, &fds);
//        if constexpr (!T_Write) ir = select(fd+1, &fds, nullptr, nullptr, &timeout);
//        else                    ir = select(fd+1, nullptr, &fds, nullptr, &timeout);
//    } while (ir==-1 && errno==EINTR);
//    if (ir < 0) throw std::system_error(errno, std::generic_category());
//    if (ir == 0) return false;
//    return true;
//}

inline size_t Read(int fd, void* data, size_t len, std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::time_point()) {
    uint8_t* d = (uint8_t*)data;
    size_t off = 0;
    
    while (off < len) {
        if (deadline.time_since_epoch().count()) {
            int fdCopy = fd;
            bool br = Select(&fdCopy, 1, nullptr, 0, deadline);
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
            int fdCopy = fd;
            bool br = Select(nullptr, 0, &fdCopy, 1, deadline);
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

inline bool Select(int* rfds, size_t rfdsLen, int* wfds, size_t wfdsLen, std::chrono::milliseconds timeout) {
    return Select(rfds, rfdsLen, wfds, wfdsLen, std::chrono::steady_clock::now()+timeout);
}

inline void Read(int fd, void* data, size_t len, std::chrono::milliseconds timeout) {
    Read(fd, data, len, std::chrono::steady_clock::now()+timeout);
}

inline void Write(int fd, const void* data, size_t len, std::chrono::milliseconds timeout) {
    Write(fd, data, len, std::chrono::steady_clock::now()+timeout);
}

} // namespace Toastbox
