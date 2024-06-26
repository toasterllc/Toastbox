#pragma once
#import "MetalUtil.h"

namespace Toastbox {

enum class CFAColor : uint8_t {
    Red     = 0,
    Green   = 1,
    Blue    = 2,
};

struct CFADesc {
    CFAColor desc[2][2] = {};
    
    template<typename T>
    CFAColor color(T x, T y) const MetalConstant { return desc[y&1][x&1]; }
    
    template<typename T>
    CFAColor color(T pos) const MetalConstant { return color(pos.x, pos.y); }
};

} // namespace Toastbox
