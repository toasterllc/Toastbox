#pragma once
#import <Cocoa/Cocoa.h>
#import <type_traits>

namespace Toastbox {

template<typename T>
T Cast(id obj) {
    using T_Class = typename std::remove_pointer<T>::type;
    assert([obj isKindOfClass:[T_Class class]]);
    return obj;
}

template<typename T>
T CastOrNull(id obj) {
    using T_Class = typename std::remove_pointer<T>::type;
    if ([obj isKindOfClass:[T_Class class]]) return obj;
    return nil;
}

#define CastProtocol(proto, obj) (id<proto>)([obj conformsToProtocol:@protocol(proto)] ? obj : nil)

template<typename Fn>
inline void TrackMouse(NSWindow* win, NSEvent* ev, Fn fn) {
    for (;;) @autoreleasepool {
        const bool mouseUp = ([ev type] == NSEventTypeLeftMouseUp);
        const bool cont = fn(ev, mouseUp);
        if (mouseUp) {
            // The mouse-up event needs to propogate into the app to complement the
            // mouse-down event. (The internal Cocoa APIs expect it.)
            [win sendEvent:ev];
        }
        
        if (mouseUp || !cont) break;
        
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
