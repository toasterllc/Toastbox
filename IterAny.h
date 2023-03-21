#pragma once
#include <variant>

namespace Toastbox {

// IterAny: wrapper that can hold either a forward iterator or a reverse iterator
template<typename T_Fwd, typename T_Rev=std::reverse_iterator<T_Fwd>>
struct IterAny : std::variant<T_Fwd,T_Rev> {
    using iterator_category = typename T_Fwd::iterator_category;
    using difference_type   = typename T_Fwd::difference_type;
    using value_type        = typename T_Fwd::value_type;
    using pointer           = typename T_Fwd::pointer;
    using reference         = typename T_Fwd::reference;
    
    using std::variant<T_Fwd,T_Rev>::variant;
    
    reference operator*() const {
        if (fwd()) return *fwdGet();
        else       return *revGet();
    }
    
    pointer operator->() {
        if (fwd()) return fwdGet().operator->();
        else       return revGet().operator->();
    }
    
    IterAny& operator++() {
        if (fwd()) ++fwdGet();
        else       ++revGet();
        return *this;
    }
    
    IterAny& operator--() {
        if (fwd()) --fwdGet();
        else       --revGet();
        return *this;
    }
    
    IterAny operator++(int) {
        IterAny x(*this);
        if (fwd()) fwdGet()++;
        else       revGet()++;
        return x;
    }
    
    IterAny operator--(int) {
        IterAny x(*this);
        if (fwd()) fwdGet()--;
        else       revGet()--;
        return x;
    }
    
    IterAny operator+(const difference_type& x) const {
        if (fwd()) return fwdGet()+x;
        else       return revGet()+x;
    }
    
    IterAny operator-(const difference_type& x) const {
        if (fwd()) return fwdGet()-x;
        else       return revGet()-x;
    }
    
    difference_type operator-(const IterAny& x) const {
        if (fwd()) return fwdGet()-x.fwdGet();
        else       return revGet()-x.revGet();
    }
    
    IterAny& operator+=(const difference_type& x) {
        if (fwd()) fwdGet()+=x;
        else       revGet()+=x;
        return *this;
    }
    
    IterAny& operator-=(const difference_type& x) {
        if (fwd()) fwdGet()-=x;
        else       revGet()-=x;
        return *this;
    }
    
    bool operator==(const IterAny& x) {
        if (fwd()) return fwdGet() == x.fwdGet();
        else       return revGet() == x.revGet();
    }
    
    bool operator!=(const IterAny& x) {
        if (fwd()) return fwdGet() != x.fwdGet();
        else       return revGet() != x.revGet();
    }
    
    bool fwd() const { return std::holds_alternative<T_Fwd>(*this); }
    bool rev() const { return std::holds_alternative<T_Rev>(*this); }
    
    const T_Fwd& fwdGet() const { return std::get<T_Fwd>(*this); }
    const T_Rev& revGet() const { return std::get<T_Rev>(*this); }
    
    T_Fwd& fwdGet() { return std::get<T_Fwd>(*this); }
    T_Rev& revGet() { return std::get<T_Rev>(*this); }
};

} // namespace Toastbox
