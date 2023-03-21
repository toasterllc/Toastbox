#pragma once
#import <map>
#import <list>

namespace Toastbox {

template <typename T_Key, typename T_Val>
class LRU {
public:
    struct ListVal;
    
private:
    using _List = std::list<ListVal>;
    using _ListIter = typename _List::iterator;
    using _ListConstIter = typename _List::const_iterator;
    using _Map = std::map<T_Key,_ListIter>;
//    using _MapIter = typename _Map::iterator;
    
public:
    struct ListVal {
        T_Key key;
        T_Val val;
    };
    
    _ListIter insert(const T_Key& key, const T_Val& val) {
        _list.push_front(ListVal{key, val});
        const auto [_, ok] = _map.insert(std::make_pair(key, _list.begin()));
        assert(ok);
        return _list.begin();
    }
    
    _ListIter insert(const T_Key& key, T_Val&& val) {
        _list.push_front(ListVal{key, std::move(val)});
        const auto [_, ok] = _map.insert(std::make_pair(key, _list.begin()));
        assert(ok);
        return _list.begin();
    }
    
//    _ListIter insert(_MapIter it, const T_Key& key, T_Val&& val) {
//        _list.push_front(ListVal{key, std::move(val)});
//        const auto [rit, ok] = _map.insert(it, std::make_pair(key, _list.begin()));
//        assert(ok);
//        return rit;
//    }
    
    void erase(_ListIter it) {
        const bool ok = _map.erase(it->key);
        assert(ok);
        _list.erase(it);
    }
    
    _ListIter get(const T_Key& key) {
        // Find element
        auto it = _map.find(key);
        if (it == _map.end()) return _list.end();
        
        // Move element to front of list
        _list.splice(_list.begin(), _list, it->second);
        // Update the map entry with its new position in _list
        it->second = _list.begin();
        return _list.begin();
    }
    
    _ListConstIter begin() const { return _list.begin(); }
    _ListConstIter end() const { return _list.end(); }
    
    const ListVal& front() const {
        assert(!_list.empty());
        return _list.front();
    }
    
    const ListVal& back() const {
        assert(!_list.empty());
        return _list.back();
    }
    
private:
    _Map _map;
    _List _list;
};

} // namespace Toastbox
