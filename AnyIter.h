#pragma once
#include <variant>

namespace Toastbox {

// AnyIter: wrapper that can hold either a forward iterator or a reverse iterator
template<typename T_Fwd, typename T_Rev=std::reverse_iterator<T_Fwd>>
struct AnyIter : std::variant<T_Fwd,T_Rev> {
    using iterator_category = typename T_Fwd::iterator_category;
    using difference_type   = typename T_Fwd::difference_type;
    using value_type        = typename T_Fwd::value_type;
    using pointer           = typename T_Fwd::pointer;
    using reference         = typename T_Fwd::reference;
    
    using std::variant<T_Fwd,T_Rev>::variant;
    
    reference operator*() const {
        if (forward()) return *forwardGet();
        else           return *reverseGet();
    }
    
    pointer operator->() {
        if (forward()) return forwardGet().operator->();
        else           return reverseGet().operator->();
    }
    
    AnyIter& operator++() {
        if (forward()) ++forwardGet();
        else           ++reverseGet();
        return *this;
    }
    
    AnyIter& operator--() {
        if (forward()) --forwardGet();
        else           --reverseGet();
        return *this;
    }
    
    AnyIter operator++(int) {
        AnyIter x(*this);
        if (forward()) forwardGet()++;
        else           reverseGet()++;
        return x;
    }
    
    AnyIter operator--(int) {
        AnyIter x(*this);
        if (forward()) forwardGet()--;
        else           reverseGet()--;
        return x;
    }
    
    AnyIter operator+(const difference_type& x) const {
        if (forward()) return forwardGet()+x;
        else           return reverseGet()+x;
    }
    
    AnyIter operator-(const difference_type& x) const {
        if (forward()) return forwardGet()-x;
        else           return reverseGet()-x;
    }
    
    difference_type operator-(const AnyIter& x) const {
        if (forward()) return forwardGet()-x.forwardGet();
        else           return reverseGet()-x.reverseGet();
    }
    
    AnyIter& operator+=(const difference_type& x) {
        if (forward()) forwardGet()+=x;
        else           reverseGet()+=x;
        return *this;
    }
    
    AnyIter& operator-=(const difference_type& x) {
        if (forward()) forwardGet()-=x;
        else           reverseGet()-=x;
        return *this;
    }
    
    bool operator==(const AnyIter& x) {
        if (forward()) return forwardGet() == x.forwardGet();
        else           return reverseGet() == x.reverseGet();
    }
    
    bool operator!=(const AnyIter& x) {
        if (forward()) return forwardGet() != x.forwardGet();
        else           return reverseGet() != x.reverseGet();
    }
    
    bool forward() const { return std::holds_alternative<T_Fwd>(*this); }
    bool reverse() const { return std::holds_alternative<T_Rev>(*this); }
    
    const T_Fwd& forwardGet() const { return std::get<T_Fwd>(*this); }
    const T_Rev& reverseGet() const { return std::get<T_Rev>(*this); }
    
    T_Fwd& forwardGet() { return std::get<T_Fwd>(*this); }
    T_Rev& reverseGet() { return std::get<T_Rev>(*this); }
};

} // namespace Toastbox
