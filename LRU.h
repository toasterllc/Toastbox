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
    
//    // insert(): unconditionally inserts a key-value pair
//    // returns an iterator to the inserted element, and whether this was the initial insertion
//    std::pair<_ListIter,bool> insert(const T_Key& key, T_Val val) {
//        _list.push_front({.key=key, .val=std::move(val)});
//        const auto [it, init] = _map.insert(std::make_pair(key, _list.begin()));
//        // If the entry already existed, erase the previous list entry
//        // and update the map entry with the new iterator.
//        if (!init) {
//            _list.erase(it->second);
//            it->second = _list.begin();
//        }
//        return std::make_pair(_list.begin(), init);
//    }
    
    
//    template<typename _T_Val>
//    _ListIter insert(const T_Key& key, _T_Val val) {
//        _list.push_front({.key=key, .val=std::move(val)});
////        _list.front().key = key;
////        _list.front().val = std::move(val);
//        const auto [it, ok] = _map.insert(std::make_pair(key, _list.begin()));
//        if (!ok) {
//            _list.erase(it->second);
//            it->second = _list.begin();
//        }
//        return _list.begin();
//    }
    
    
    
//    _ListIter insert(const T_Key& key, T_Val&& val) {
//        _list.push_front({.key=key, .val=std::move(val)});
////        _list.front().key = key;
////        _list.front().val = std::move(val);
//        const auto [it, ok] = _map.insert(std::make_pair(key, _list.begin()));
//        if (!ok) {
//            _list.erase(it->second);
//            it->second = _list.begin();
//        }
//        return _list.begin();
//    }
    
    
//    template <typename _T_Val>
//    _ListIter insert(const T_Key& key, _T_Val val) {
//        _list.push_front(ListVal{key, std::move(val)});
//        const auto [it, ok] = _map.insert(std::make_pair(key, _list.begin()));
//        if (!ok) {
//            _list.erase(it->second);
//            it->second = _list.begin();
//        }
//        return _list.begin();
//    }
    
//    _ListIter insert(const T_Key& key, T_Val val) {
//        return insert(key, std::move(val));
//    }
    
//    _ListIter insert(_MapIter it, const T_Key& key, T_Val&& val) {
//        _list.push_front(ListVal{key, std::move(val)});
//        const auto [rit, ok] = _map.insert(it, std::make_pair(key, _list.begin()));
//        assert(ok);
//        return rit;
//    }
    
    void erase(_ListConstIter it) {
        const bool ok = _map.erase(it->key);
        assert(ok);
        _list.erase(it);
    }
    
    T_Val& operator[] (const T_Key& key) {
        _list.push_front({.key=key});
        const auto [it, init] = _map.insert(std::make_pair(key, _list.begin()));
        // If the entry already existed, erase the previous list entry
        if (!init) {
            _list.erase(it->second);
            it->second = _list.begin();
        }
        return it->second->val;
    }
    
    _ListIter find(const T_Key& key) {
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
