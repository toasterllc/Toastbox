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
        if (std::holds_alternative<T_Fwd>(*this)) return *std::get<T_Fwd>(*this);
        else                                      return *std::get<T_Rev>(*this);
    }
    
    pointer operator->() {
        if (std::holds_alternative<T_Fwd>(*this)) return std::get<T_Fwd>(*this).operator->();
        else                                      return std::get<T_Rev>(*this).operator->();
    }
    
    AnyIter& operator++() {
        if (std::holds_alternative<T_Fwd>(*this)) ++std::get<T_Fwd>(*this);
        else                                      ++std::get<T_Rev>(*this);
        return *this;
    }
    
    AnyIter& operator--() {
        if (std::holds_alternative<T_Fwd>(*this)) --std::get<T_Fwd>(*this);
        else                                      --std::get<T_Rev>(*this);
        return *this;
    }
    
    AnyIter operator++(int) {
        AnyIter x(*this);
        if (std::holds_alternative<T_Fwd>(*this)) std::get<T_Fwd>(*this)++;
        else                                      std::get<T_Rev>(*this)++;
        return x;
    }
    
    AnyIter operator--(int) {
        AnyIter x(*this);
        if (std::holds_alternative<T_Fwd>(*this)) std::get<T_Fwd>(*this)--;
        else                                      std::get<T_Rev>(*this)--;
        return x;
    }
    
    AnyIter operator+(const difference_type& x) const {
        if (std::holds_alternative<T_Fwd>(*this)) return std::get<T_Fwd>(*this)+x;
        else                                      return std::get<T_Rev>(*this)+x;
    }
    
    AnyIter operator-(const difference_type& x) const {
        if (std::holds_alternative<T_Fwd>(*this)) return std::get<T_Fwd>(*this)-x;
        else                                      return std::get<T_Rev>(*this)-x;
    }
    
    difference_type operator-(const AnyIter& x) const {
        if (std::holds_alternative<T_Fwd>(*this)) return std::get<T_Fwd>(*this)-std::get<T_Fwd>(x);
        else                                      return std::get<T_Rev>(*this)-std::get<T_Rev>(x);
    }
    
    AnyIter& operator+=(const difference_type& x) {
        if (std::holds_alternative<T_Fwd>(*this)) std::get<T_Fwd>(*this)+=x;
        else                                      std::get<T_Rev>(*this)+=x;
        return *this;
    }
    
    AnyIter& operator-=(const difference_type& x) {
        if (std::holds_alternative<T_Fwd>(*this)) std::get<T_Fwd>(*this)-=x;
        else                                      std::get<T_Rev>(*this)-=x;
        return *this;
    }
    
    bool operator==(const AnyIter& x) {
        if (std::holds_alternative<T_Fwd>(*this)) return std::get<T_Fwd>(*this) == std::get<T_Fwd>(x);
        else                                      return std::get<T_Rev>(*this) == std::get<T_Rev>(x);
    }
    
    bool operator!=(const AnyIter& x) {
        if (std::holds_alternative<T_Fwd>(*this)) return std::get<T_Fwd>(*this) != std::get<T_Fwd>(x);
        else                                      return std::get<T_Rev>(*this) != std::get<T_Rev>(x);
    }
};

} // namespace Toastbox
