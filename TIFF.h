#include <vector>
#include <limits>
#include <utility>

namespace Toastbox {

struct TIFF {
    static constexpr uint16_t Byte      = 1;
    static constexpr uint16_t ASCII     = 2;
    static constexpr uint16_t Short     = 3;
    static constexpr uint16_t Long      = 4;
    static constexpr uint16_t Rational  = 5;
    static constexpr uint16_t Undefined = 7;
    static constexpr uint16_t SRational = 10;
    
    // From https://github.com/syoyo/tinydng/blob/release/tiny_dng_writer.h
    static std::tuple<int32_t,int32_t> _RationalForDouble(double x) {
        if (!std::isfinite(x)) {
            return std::make_tuple(x>0 ? 1 : -1, 0);
        }
        
        int bdigits = FLT_MANT_DIG;
        int expo = 0;
        double num = std::frexp(x, &expo) * std::pow(2, bdigits);
        double den = 1;
        expo -= bdigits;
        
        if (expo > 0) {
            num *= std::pow(2, expo);
        
        } else if (expo < 0) {
            expo = -expo;
            if (expo >= FLT_MAX_EXP-1) {
                num /= std::pow(2, expo - (FLT_MAX_EXP - 1));
                den *= std::pow(2, FLT_MAX_EXP - 1);
                return std::make_tuple(num, den);
            } else {
                den *= std::pow(2, expo);
            }
        }
        
        while (((std::fabs(num) > 0) &&
            (std::fabs(std::fmod(num, 2)) < std::numeric_limits<double>::epsilon()) &&
            (std::fabs(std::fmod(den, 2)) < std::numeric_limits<double>::epsilon())) ||
            (std::max(std::fabs(num), std::fabs(den)) > std::numeric_limits<int32_t>::max())) {
            
            num /= 2;
            den /= 2;
        }
        
        return std::make_tuple((int32_t)num, (int32_t)den);
    }
    
    template<typename T>
    struct Val {
        size_t off = 0;
    };
    
    template<typename T>
    void push(T t={}) {
        _data.insert(_data.end(), (uint8_t*)&t, (uint8_t*)&t+sizeof(t));
    }
    
    template<typename T>
    void push(Val<T>& val) {
        val.off = _data.size();
        push((T)0);
    }
    
    void push(uint16_t tag, uint16_t type, uint32_t count, uint32_t val) {
        push(tag);
        push(type);
        push(count);
        push(val);
    }
    
    void push(uint16_t tag, uint16_t type, uint32_t count, Val<uint32_t>& val) {
        push(tag);
        push(type);
        push(count);
        push(val);
    }
    
    void push(float x) {
        const auto [num, den] = _RationalForDouble(x);
        push(num);
        push(den);
    }
    
    void push(double x) {
        const auto [num, den] = _RationalForDouble(x);
        push(num);
        push(den);
    }
    
    void push(const void* data, size_t len) {
        _data.insert(_data.end(), (const uint8_t*)data, (const uint8_t*)data+len);
    }
    
    template<typename T>
    void push(T begin, T end) {
        for (T it=begin; it!=end; it++) {
            push(*it);
        }
    }
    
//    template<size_t T_SIZE>
//    void push(const double (&x)[T_SIZE]) {
//        for (double d : x) push(d);
//    }
    
    template<typename T>
    void set(Val<T> val, T t) {
        memcpy(_data.data()+val.off, &t, sizeof(t));
    }
    
    uint32_t off() {
        return (uint32_t)_data.size();
    }
    
    void write(const std::filesystem::path& filePath) {
        std::ofstream f;
        f.exceptions(std::ofstream::failbit | std::ofstream::badbit);
        f.open(filePath);
        f.write((const char*)_data.data(), _data.size());
    }
    
    std::vector<uint8_t> _data;
};

} // namespace Toastbox
