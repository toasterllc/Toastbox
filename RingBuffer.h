#pragma once
#include <cstdlib>
#include <algorithm>

template <typename T, size_t Cap>
class RingBuffer {
public:
    
    size_t len() const {
        if (_woff > _roff)      return _woff-_roff;
        else if (_woff < _roff) return (Cap-_roff) + _woff;
        else if (_full)         return Cap;
        return 0;
    }
    
    size_t space() const {
        return Cap-len();
    }
    
    void read(T* data, size_t len) {
        // Not using assert() so we can use this on STM32
        if (len > this->len()) abort();
        
        // Read segment 1 (_roff to end)
        size_t rem = len;
        const size_t len1 = std::min(rem, Cap-_roff);
        std::copy(_buf+_roff, _buf+_roff+len1, data);
        rem -= len1;
        _roff += len1;
        if (_roff == Cap) _roff = 0;
        
        // Read segment 2 (0 to _woff)
        if (rem) {
            const size_t len2 = std::min(rem, _woff);
            std::copy(_buf, _buf+len2, data+len1);
            rem -= len2;
            _roff += len2;
            if (_roff == Cap) _roff = 0;
        }
        
        // If we read something, we're no longer full
        if (len) _full = false;
    }
    
    T read() {
        T t;
        read(&t, 1);
        return t;
    }
    
    template <bool Overwrite>
    void write(const T* data, size_t len) {
        if constexpr (!Overwrite) {
            // Not using assert() so we can use this on STM32
            if (len > space()) abort();
        } else {
            // Overwriting is allowed
            
            // Check if we're writing more elements than our capacity.
            // If so, only write the trailing `Cap` elements.
            if (len > Cap) {
                data += len-Cap;
                len = Cap;
            }
            
            // Update _roff according to the amount of data being overwritten
            const size_t avail = space();
            if (len > avail) {
                const size_t overflow = len-avail;
                if (overflow < Cap-_roff) {
                    _roff += overflow;
                } else {
                    _roff = overflow-(Cap-_roff);
                }
            }
        }
        
        // Write segment 1 (_woff to end)
        size_t rem = len;
        const size_t len1 = std::min(rem, Cap-_woff);
        std::copy(data, data+len1, _buf+_woff);
        rem -= len1;
        _woff += len1;
        if (_woff == Cap) _woff = 0;
        
        // Write segment 2 (0 to _roff)
        if (rem) {
            const size_t len2 = std::min(rem, _roff);
            std::copy(data+len1, data+len1+len2, _buf);
            rem -= len2;
            _woff += len2;
            if (_woff == Cap) _woff = 0;
        }
        
        // If we wrote until _woff==_roff, then we're full
        if (len && _woff==_roff) _full = true;
    }
    
    void write(const T* data, size_t len)       { write<false>(data, len);  }
    void write(T t)                             { write<false>(&t, 1);      }
    void writeOver(const T* data, size_t len)   { write<true>(data, len);   }
    void writeOver(T t)                         { write<true>(&t, 1);       }
    
private:
    size_t _roff = 0;
    size_t _woff = 0;
    bool _full = false;
    T _buf[Cap];
};
