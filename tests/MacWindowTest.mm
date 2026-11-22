#include <native_desktop/Desktop.h>
#import <Cocoa/Cocoa.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
// Inside the namespace, its names are found before any of AppKit's
namespace native_desktop {

static NSWindow* CreateWindow () {
 NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect (100, 100, 64, 64) styleMask:NSWindowStyleMaskTitled
  backing:NSBackingStoreBuffered defer:NO];
 [window setReleasedWhenClosed:NO];
 return window;
}
static Window Borrow (NSWindow* window) {
 Window borrowed;
 borrowed.handle = (__bridge void*)window;
 return borrowed;
}
// Where the window is in the screen's front to back order, or -1 when it is not on screen
static NSInteger StackPosition (NSWindow* window) {
 NSArray<NSNumber*>* numbers = [NSWindow windowNumbersWithOptions:0];
 for (NSUInteger i = 0; i < [numbers count]; i++) {
  if ([numbers[i] integerValue] == [window windowNumber]) return (NSInteger)i;
 }
 return -1;
}
static int Run () {
 [NSApplication sharedApplication];
 [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
 [NSApp finishLaunching];
 NSWindow* first = CreateWindow ();
 NSWindow* second = CreateWindow ();
 Window a = Borrow (first);
 Window b = Borrow (second);

 // A window made and never shown is not ordered, as AppKit would put it on screen to order it.
 assert (![first isVisible] && ![second isVisible]);
 assert (!WindowControl::OrderToBack (&a));
 assert (!WindowControl::OrderAbove (&a, &b));
 assert (!WindowControl::OrderBelow (&a, &b));
 assert (![first isVisible] && ![second isVisible]);

 // Nor is a shown window ordered against a hidden one, which would stay hidden.
 [first orderFront:nil];
 assert ([first isVisible]);
 assert (!WindowControl::OrderAbove (&a, &b));
 assert (!WindowControl::OrderBelow (&a, &b));
 assert (![second isVisible]);

 // Shown windows are stacked as asked, and a window is not ordered against itself.
 [second orderFront:nil];
 assert ([second isVisible]);
 assert (!WindowControl::OrderAbove (&a, &a));
 assert (WindowControl::OrderAbove (&a, &b));
 assert (StackPosition (first) >= 0 && StackPosition (first) < StackPosition (second));
 assert (WindowControl::OrderBelow (&a, &b));
 assert (StackPosition (second) >= 0 && StackPosition (second) < StackPosition (first));

 // A window hidden again is left hidden.
 [first orderOut:nil];
 assert (!WindowControl::OrderToBack (&a));
 assert (!WindowControl::OrderAbove (&a, &b));
 assert (![first isVisible]);

 [first close];
 [second close];
 std::cout << "macOS window stacking, and hidden windows left hidden, passed\n";
 return 0;
}
}
int main () {
 @autoreleasepool {
  return native_desktop::Run ();
 }
}
