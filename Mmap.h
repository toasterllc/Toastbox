#pragma once
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <stdexcept>
#include <string>
#include <cstring>
#include <filesystem>
#include <optional>
#include <unistd.h>
#include "RuntimeError.h"
#include "FileDescriptor.h"

namespace Toastbox {

class Mmap {
public:
    static size_t PageSize() {
        static const size_t X = getpagesize();
        return X;
    }
    
    static size_t PageFloor(size_t x) {
        return (x/PageSize())*PageSize();
    }
    
    static size_t PageCeil(size_t x) {
        return PageFloor(x+PageSize()-1);
    }
    
    Mmap() {}
    
    Mmap(FileDescriptor&& fd, std::optional<size_t> cap=std::nullopt, int oflags=O_RDONLY) {
        _init(std::move(fd), cap, oflags);
    }
    
    Mmap(const std::filesystem::path& path, std::optional<size_t> cap=std::nullopt, int oflags=O_RDONLY) {
        int fd = open(path.c_str(), oflags);
        if (fd < 0) throw RuntimeError("open failed: %s", strerror(errno));
        _init(fd, cap, oflags);
    }
    
    // Copy: deleted
    Mmap(const Mmap& x) = delete;
    Mmap& operator=(const Mmap& x) = delete;
    // Move: allowed
    Mmap(Mmap&& x) { swap(x); }
    Mmap& operator=(Mmap&& x) { swap(x); return *this; }
    
    ~Mmap() {
        if (_state.data) {
            munmap((void*)_state.data, _state.cap);
        }
    }
    
    void swap(Mmap& x) {
        std::swap(_state, x._state);
    }
    
    void sync() const {
        if (!_state.data) throw RuntimeError("invalid state");
        if (!_state.len) return; // Short-circuit if there's nothing to sync
        int ir = msync(_state.data, _state.len, MS_SYNC);
        if (ir) throw RuntimeError("msync failed: %s", strerror(errno));
    }
    
    uint8_t* data(size_t off=0, size_t len=0) {
        return const_cast<uint8_t*>(((const Mmap*)this)->data(off, len));
    }
    
    const uint8_t* data(size_t off=0, size_t len=0) const {
        if (off>_state.len || (_state.len-off)<len) {
            const uintmax_t validBegin = 0;
            const uintmax_t validEnd = _state.len;
            const uintmax_t accessBegin = off;
            const uintmax_t accessEnd = off+len;
            throw RuntimeError("access beyond valid region (valid: [0x%jx,0x%jx), accessed: [0x%jx,0x%jx))",
                validBegin, validEnd,
                accessBegin, accessEnd
            );
        }
        return (const uint8_t*)(_state.data+off);
    }
    
    size_t len() const { return _state.len; }
    
    // len(x): resize underlying file within the range of the original capacity
    // If the file is expanded, the additional pages are mapped to the file.
    void len(size_t l) {
        assert(l <= _state.cap);
        if (l == _state.len) return; // Short-circuit if nothing changed
        
        const size_t lenPrev = _state.len;
        _state.len = l;
        
        const int ir = ftruncate(_state.fd, _state.len);
        if (ir) throw Toastbox::RuntimeError("ftruncate failed: %s", strerror(errno));
        
        // If the file was expanded, remap the affected pages to the file.
        // (If the file was contracted, we don't need to do anything.)
        if (_state.len > lenPrev) {
            const size_t begin = PageFloor(lenPrev);
            const size_t end   = PageCeil(_state.len);
            void* data = mmap(_state.data+begin, end-begin, _MmapProtection(_state.oflags),
                _MmapFlags(_state.oflags)|MAP_FIXED, _state.fd, begin);
            if (data == MAP_FAILED) throw RuntimeError("mmap failed: %s", strerror(errno));
        }
    }
    
    size_t cap() const { return _state.cap; }
    
private:
    static constexpr int _MmapProtection(int oflags) {
        switch (oflags & O_ACCMODE) {
        case O_RDONLY:  return PROT_READ;
        case O_WRONLY:
        case O_RDWR:    return PROT_READ|PROT_WRITE;
        default:        abort();
        }
    }
    
    static constexpr int _MmapFlags(int oflags) {
        switch (oflags & O_ACCMODE) {
        case O_RDONLY:  return MAP_PRIVATE;
        case O_WRONLY:
        case O_RDWR:    return MAP_SHARED;
        default:        abort();
        }
    }
    
    void _init(FileDescriptor&& fd, std::optional<size_t> cap, int oflags) {
        assert(!cap || *cap==PageCeil(*cap));
        
        _state.fd = std::move(fd);
        _state.oflags = oflags;
        
        // Determine file size
        struct stat st;
        int ir = fstat(_state.fd, &st);
        if (ir) throw RuntimeError("fstat failed: %s", strerror(errno));
        const size_t fileLen = st.st_size;
        
        // No capacity specified: len=file length, cap=ceiled file length
        if (!cap) {
            _state.len = fileLen;
            _state.cap = PageCeil(fileLen);
        
        // Capacity specified: len=min(cap, fileLen), cap=cap
        } else {
            _state.len = std::min(*cap, fileLen);
            _state.cap = *cap;
        }
        
        void* data = mmap(nullptr, _state.cap, _MmapProtection(_state.oflags), _MmapFlags(_state.oflags), _state.fd, 0);
        if (data == MAP_FAILED) throw RuntimeError("mmap failed: %s", strerror(errno));
        _state.data = (uint8_t*)data;
    }
    
    struct {
        FileDescriptor fd;
        int oflags = 0;
        uint8_t* data = nullptr;
        size_t len = 0;
        size_t cap = 0;
    } _state = {};
};

} // namespace Toastbox
