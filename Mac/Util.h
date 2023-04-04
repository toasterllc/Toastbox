#pragma once
#import <Cocoa/Cocoa.h>

namespace Toastbox {

template <typename T>
T Cast(id obj) {
    using T_Class = typename std::remove_pointer<T>::type;
    assert([obj isKindOfClass:[T_Class class]]);
    return obj;
}

template <typename T>
T CastOrNull(id obj) {
    using T_Class = typename std::remove_pointer<T>::type;
    if ([obj isKindOfClass:[T_Class class]]) return obj;
    return nil;
}

#define CastProtocol(proto, obj) (id<proto>)([obj conformsToProtocol:@protocol(proto)] ? obj : nil)

template <typename Fn>
inline void TrackMouse(NSWindow* win, NSEvent* ev, Fn fn) {
    for (;;) @autoreleasepool {
        const bool done = ([ev type] == NSEventTypeLeftMouseUp);
        fn(ev, done);
        if (done) {
            // The mouse-up event needs to propogate into the app to complement the
            // mouse-down event. (The internal Cocoa APIs expect it.)
            [win sendEvent:ev];
            break;
        }
        
        ev = [win nextEventMatchingMask:
            NSEventMaskLeftMouseDown    |
            NSEventMaskLeftMouseDragged |
            NSEventMaskLeftMouseUp      |
            // NSEventMaskFlagsChanged: allows shift key press/depress to be detected
            // while dragging mouse (eg so selections can change dynamically
            // depending on shift key state)
            NSEventMaskFlagsChanged
        ];
    }
}

inline NSDictionary* LayerNullActions = @{
    kCAOnOrderIn: [NSNull null],
    kCAOnOrderOut: [NSNull null],
    @"bounds": [NSNull null],
    @"frame": [NSNull null],
    @"position": [NSNull null],
    @"sublayers": [NSNull null],
    @"transform": [NSNull null],
    @"contents": [NSNull null],
    @"contentsScale": [NSNull null],
    @"hidden": [NSNull null],
    @"fillColor": [NSNull null],
    @"fontSize": [NSNull null],
};

} // namespace Toastbox

namespace std {

// Until we can use C++23's std::to_underlying()
template<class Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum e) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(e);
}

} // namespace std
