#pragma once
#include <mutex>
#include <condition_variable>

namespace Toastbox {

class Signal {
public:
    struct Stop : std::exception {};
    
    template <typename T_Cond>
    void wait(std::unique_lock<std::mutex> lock, T_Cond cond) {
        _cv.wait(lock, [&] {
            if (_stop) throw Stop();
            return cond();
        });
    }
    
    template <typename T_Cond>
    auto wait(T_Cond cond) {
        auto l = std::unique_lock(_lock);
        wait(l, cond);
        return l;
    }
    
    std::unique_lock<std::mutex> lock() {
        auto l = std::unique_lock(_lock);
        if (_stop) throw Stop();
        return l;
    }
    
    void signalOne() { _cv.notify_one(); }
    void signalAll() { _cv.notify_all(); }
    
    void stop() {
        {
            auto l = std::unique_lock(_lock);
            _stop = true;
        }
        _cv.notify_all();
    }
    
private:
    std::mutex _lock;
    std::condition_variable _cv;
    bool _stop = false;
};

} // namespace Toastbox
