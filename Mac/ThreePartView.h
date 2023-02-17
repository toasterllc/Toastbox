#import <Cocoa/Cocoa.h>

//// ThreePartViewContentView: informal protocol instead of a real @protocol, so that content view
//// classes don't have a dependency on ThreePartView
//@interface NSView (ThreePartViewContentView)
//- (NSView*)initialFirstResponder;
//@end

@interface ThreePartView : NSView

- (NSView*)leftView;
- (void)setLeftView:(NSView*)view;

- (NSView*)centerView;
- (void)setCenterView:(NSView*)view;

- (NSView*)rightView;
- (void)setRightView:(NSView*)view;

@end
