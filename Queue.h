#pragma once
#include <cstddef>
#include <type_traits>

namespace Toastbox {

// Queue:
//   Queue is a statically-sized queue that manages `T_Count` items
//   to facilitate producer-consumer schemes.
//   
//   If the Queue is writable (wok()==true), the writer writes
//   into the item returned by wget(), and when writing is
//   complete, calls wpush().
//   
//   If the Queue is readable (rok()==true), the reader reads
//   from the item returned by rget(), and when reading is
//   complete, calls rpop().

template<typename T_Item, size_t T_Count, bool T_FullReset=false, auto T_Assert=nullptr>
class Queue {
public:
    // Read
    bool rok() const { return _w!=_r || _full; }
    
    T_Item& rget() {
        _Assert(rok());
        return _items[_r];
    }
    
    void rpop() {
        _Assert(rok());
        _r++;
        if (_r == T_Count) _r = 0;
        _full = false;
    }
    
    // Write
    bool wok() const { return !_full; }
    
    T_Item& wget() {
        _Assert(wok());
        return _items[_w];
    }
    
    void wpush() {
        _Assert(wok());
        _w++;
        if (_w == T_Count) _w = 0;
        if (_w == _r) _full = true;
    }
    
    // Reset
    void reset() {
        _w = 0;
        _r = 0;
        _full = T_FullReset;
    }
    
private:
    T_Item _items[T_Count];
    size_t _w = 0;
    size_t _r = 0;
    bool _full = T_FullReset;
    
    static void _Assert(bool c) {
        if constexpr (!std::is_same<decltype(T_Assert), std::nullptr_t>::value) {
            T_Assert(c);
        }
    }
};

} // namespace Toastbox
