#import "ThreePartView.h"
#import <algorithm>
#import <cmath>
#import "Util.h"
using namespace Toastbox;

namespace LeftRightWidth {
    static constexpr CGFloat HideThreshold  = 50;
    static constexpr CGFloat Min            = 150;
    static constexpr CGFloat Default        = 220;
}

namespace CenterWidth {
    static constexpr CGFloat Min = 200;
}

#define ResizerView ThreePartView_ResizerView

using ResizerViewHandler = void(^)(NSEvent* event);

@interface ResizerView : NSView
@end

@implementation ResizerView {
@public
    ResizerViewHandler handler;
    NSCursor* cursor;
}

- (instancetype)initWithFrame:(NSRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    [self setTranslatesAutoresizingMaskIntoConstraints:false];
    return self;
}

- (void)setFrame:(NSRect)frame {
    [super setFrame:frame];
    [[self window] invalidateCursorRectsForView:self];
}

- (void)resetCursorRects {
    [self addCursorRect:[self bounds] cursor:cursor];
}

- (void)mouseDown:(NSEvent*)event {
    handler(event);
}

@end


@interface ThreePartView ()
@end

struct AuxView {
    NSView* view = nil;
    NSView* containerView = nil;
    ResizerView* resizerView = nil;
    NSLayoutConstraint* width = nil;
    NSLayoutConstraint* widthMin = nil;
    bool visible = false;
};

@implementation ThreePartView {
    AuxView _left;
    AuxView _right;
    
    NSView* _centerView;
    NSView* _centerThreePartView;
    
    bool _dragging;
}

- (instancetype)initWithCoder:(NSCoder*)coder {
    if (!(self = [super initWithCoder:coder])) return nil;
    [self initCommon];
    return self;
}

- (instancetype)initWithFrame:(NSRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    [self initCommon];
    return self;
}

static void _AuxViewInit(ThreePartView* self, AuxView& auxView, bool left) {
    {
        auxView.containerView = [[NSView alloc] initWithFrame:{}];
        [auxView.containerView setTranslatesAutoresizingMaskIntoConstraints:false];
        [self addSubview:auxView.containerView];
        
        auxView.width = [NSLayoutConstraint constraintWithItem:auxView.containerView attribute:NSLayoutAttributeWidth
            relatedBy:NSLayoutRelationEqual toItem:nil attribute:NSLayoutAttributeNotAnAttribute
            multiplier:0 constant:LeftRightWidth::Default];
        [auxView.width setPriority:NSLayoutPriorityDragThatCannotResizeWindow];
        [auxView.width setActive:true];
        
        auxView.widthMin = [NSLayoutConstraint constraintWithItem:auxView.containerView attribute:NSLayoutAttributeWidth
            relatedBy:NSLayoutRelationGreaterThanOrEqual toItem:nil attribute:NSLayoutAttributeNotAnAttribute
            multiplier:0 constant:LeftRightWidth::Min];
        [auxView.widthMin setActive:true];
        
        NSView* x = auxView.containerView;
        [NSLayoutConstraint activateConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|[x]|"
            options:0 metrics:nil views:NSDictionaryOfVariableBindings(x)]];
    }
    
    // Resizer
    {
        constexpr CGFloat ResizerWidth = 20;
        __weak auto selfWeak = self;
        auxView.resizerView = [[ResizerView alloc] initWithFrame:{}];
        auxView.resizerView->handler = ^(NSEvent* event) { [selfWeak _auxView:auxView trackResize:event]; };
        auxView.resizerView->cursor = [NSCursor resizeLeftRightCursor];
        
        [self addSubview:auxView.resizerView];
        [[NSLayoutConstraint constraintWithItem:auxView.resizerView attribute:NSLayoutAttributeWidth
            relatedBy:NSLayoutRelationEqual toItem:nil attribute:NSLayoutAttributeNotAnAttribute multiplier:1 constant:ResizerWidth] setActive:true];
        
        [[NSLayoutConstraint constraintWithItem:auxView.resizerView attribute:NSLayoutAttributeHeight
            relatedBy:NSLayoutRelationEqual toItem:self attribute:NSLayoutAttributeHeight multiplier:1 constant:0] setActive:true];
        
        [[NSLayoutConstraint constraintWithItem:auxView.resizerView attribute:NSLayoutAttributeCenterX
            relatedBy:NSLayoutRelationEqual toItem:auxView.containerView attribute:(left ? NSLayoutAttributeRight : NSLayoutAttributeLeft)
            multiplier:1 constant:0] setActive:true];
    }
}

static void _ThreePartViewSet(NSView* containerView, NSView*__strong& lhs, NSView* rhs) {
    [lhs removeFromSuperview];
    lhs = rhs;
    [containerView addSubview:lhs];
    
    [NSLayoutConstraint activateConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|[lhs]|"
        options:0 metrics:nil views:NSDictionaryOfVariableBindings(lhs)]];
    [NSLayoutConstraint activateConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|[lhs]|"
        options:0 metrics:nil views:NSDictionaryOfVariableBindings(lhs)]];
}

- (void)initCommon {
    // Configure `self`
    {
        [self setTranslatesAutoresizingMaskIntoConstraints:false];
        [self setWantsLayer:true];
//        [[self layer] setBackgroundColor:[[NSColor colorWithSRGBRed:WindowBackgroundColor.srgb[0]
//            green:WindowBackgroundColor.srgb[1] blue:WindowBackgroundColor.srgb[2] alpha:1] CGColor]];
    }
    
    _AuxViewInit(self, _left, true);
    _AuxViewInit(self, _right, false);
    
    // Create content container view
    {
        _centerThreePartView = [[NSView alloc] initWithFrame:{}];
//        [_centerThreePartView setWantsLayer:true];
        [_centerThreePartView setTranslatesAutoresizingMaskIntoConstraints:false];
        [self addSubview:_centerThreePartView];
        
        NSView* l = _left.containerView;
        NSView* c = _centerThreePartView;
        NSView* r = _right.containerView;
        
        [NSLayoutConstraint activateConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|[l][c][r]|"
            options:0 metrics:nil views:NSDictionaryOfVariableBindings(l, c, r)]];
        
        [NSLayoutConstraint activateConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|[_centerThreePartView]|"
            options:0 metrics:nil views:NSDictionaryOfVariableBindings(_centerThreePartView)]];
        
        [[NSLayoutConstraint constraintWithItem:_centerThreePartView attribute:NSLayoutAttributeWidth
            relatedBy:NSLayoutRelationGreaterThanOrEqual toItem:nil attribute:NSLayoutAttributeNotAnAttribute
            multiplier:0 constant:CenterWidth::Min] setActive:true];
        
    }
}

//- (SourceListView*)sourceListView {
//    return _sourceListView;
//}
//
//- (NSView*)centerView {
//    return _centerView;
//}
//
//- (void)setCenterView:(NSView*)centerView animation:(ThreePartViewAnimation)animation {
//    constexpr CFTimeInterval AnimationDuration = .2;
//    const CAMediaTimingFunctionName AnimationTimingFunction = kCAMediaTimingFunctionEaseOut;
//    NSString*const AnimationName = @"slide";
//    const CGFloat slideWidth = [_contentThreePartView bounds].size.width;
//    
//    NSView* oldCenterView = _centerView;
//    if (_centerView && animation==ThreePartViewAnimation::None) {
//        [_centerView removeFromSuperview];
//    }
//    
//    _centerView = centerView;
//    if (!_centerView) return;
//    
//    [_centerView setTranslatesAutoresizingMaskIntoConstraints:false];
//    [_contentThreePartView addSubview:centerView];
//    [NSLayoutConstraint activateConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|[_centerView]|"
//        options:0 metrics:nil views:NSDictionaryOfVariableBindings(_centerView)]];
//    [NSLayoutConstraint activateConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|[_centerView]|"
//        options:0 metrics:nil views:NSDictionaryOfVariableBindings(_centerView)]];
//    
////    if (_centerView) {
////        if ([_centerView respondsToSelector:@selector(initialFirstResponder)]) {
////            [[self window] setInitialFirstResponder:[_centerView initialFirstResponder]];
////            [[self window] makeFirstResponder:[_centerView initialFirstResponder]];
////        }
////    }
//}

- (void)_auxView:(AuxView&)auxView trackResize:(NSEvent*)event {
    NSWindow* win = [self window];
    _dragging = true;
    [win invalidateCursorRectsForView:self];
    [auxView.view viewWillStartLiveResize];
    [_centerView viewWillStartLiveResize];
    
    const CGFloat offsetX =
        [auxView.resizerView bounds].size.width/2 - [auxView.resizerView convertPoint:[event locationInWindow] fromView:nil].x;
    
    TrackMouse(win, event, [&](NSEvent* event, bool done) {
        CGFloat desiredWidth = 0;
        if (&auxView == &_left) {
            desiredWidth = [self convertPoint:[event locationInWindow] fromView:nil].x + offsetX;
        } else if (&auxView == &_right) {
            desiredWidth = ([[win contentView] bounds].size.width - [self convertPoint:[event locationInWindow] fromView:nil].x) - offsetX;
        } else {
            abort();
        }
        
        const CGFloat width = std::round(std::max(LeftRightWidth::Min, desiredWidth));
        
        if (desiredWidth<LeftRightWidth::HideThreshold && auxView.visible) {
            [auxView.width setConstant:0];
            auxView.visible = false;
        
        } else if (desiredWidth>=LeftRightWidth::HideThreshold && !auxView.visible) {
            [auxView.width setConstant:width];
            auxView.visible = true;
        
        } else if (auxView.visible) {
            [auxView.width setConstant:width];
        }
        
        [auxView.widthMin setActive:auxView.visible];
    });
    
    _dragging = false;
    [auxView.view viewDidEndLiveResize];
    [_centerView viewDidEndLiveResize];
    [win invalidateCursorRectsForView:self];
}

- (void)resetCursorRects {
    if (_dragging) {
        [self addCursorRect:[self bounds] cursor:_left.resizerView->cursor];
        [self addCursorRect:[self bounds] cursor:_right.resizerView->cursor];
    }
}

- (NSView*)leftView {
    return _left.view;
}

- (void)setLeftView:(NSView*)view {
    _ThreePartViewSet(_left.containerView, _left.view, view);
}

- (NSView*)centerView {
    return _centerView;
}

- (void)setCenterView:(NSView*)view {
    _ThreePartViewSet(_centerThreePartView, _centerView, view);
}

- (NSView*)rightView {
    return _right.view;
}

- (void)setRightView:(NSView*)view {
    _ThreePartViewSet(_right.containerView, _right.view, view);
}

@end
