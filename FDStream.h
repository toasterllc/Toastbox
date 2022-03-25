#pragma once

namespace Toastbox {

class FDStream {
protected:
#ifdef __GLIBCXX__
    // GCC (libstdc++)
    using _Filebuf = __gnu_cxx::stdio_filebuf<char>;
    _Filebuf* _InitFilebuf(int fd, std::ios_base::openmode mode) {
        _filebuf = _Filebuf(fd, mode);
        return &_filebuf;
    }
#else
    // Clang (libc++)
    using _Filebuf = std::basic_filebuf<char>;
    _Filebuf* _InitFilebuf(int fd, std::ios_base::openmode mode) {
        _filebuf.__open(fd, mode);
        return &_filebuf;
    }
#endif
    
    _Filebuf _filebuf;
};

class FDStreamIn : FDStream, public std::istream {
public:
    FDStreamIn(int fd) : std::istream(_InitFilebuf(fd, std::ios::in)) {}
};

class FDStreamOut : FDStream, public std::ostream {
public:
    FDStreamOut(int fd) : std::ostream(_InitFilebuf(fd, std::ios::out)) {}
};

class FDStreamInOut : FDStream, public std::iostream {
public:
    FDStreamInOut(int fd) : std::iostream(_InitFilebuf(fd, std::ios::in|std::ios::out)) {}
};

}
