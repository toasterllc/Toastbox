#pragma once
#include <variant>

namespace Toastbox {

// AnyIter: wrapper that can hold either a forward iterator or a reverse iterator
template<typename T_Fwd, typename T_Rev=std::reverse_iterator<T_Fwd>>
struct AnyIter : std::variant<T_Fwd,T_Rev> {
    using std::variant<T_Fwd,T_Rev>::variant;
    
    typename T_Fwd::reference operator*() const {
        if (std::holds_alternative<T_Fwd>(*this)) return *std::get<T_Fwd>(*this);
        else                                      return *std::get<T_Rev>(*this);
    }
    
    typename T_Fwd::pointer operator->() const {
        if (std::holds_alternative<T_Fwd>(*this)) return std::get<T_Fwd>(*this).operator->();
        else                                      return std::get<T_Rev>(*this).operator->();
    }
    
    AnyIter operator++(int) {
        if (std::holds_alternative<T_Fwd>(*this)) std::get<T_Fwd>(*this)++;
        else                                      std::get<T_Rev>(*this)++;
        return *this;
    }
};

} // namespace Toastbox
