#include <native_desktop/Desktop.h>
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

// A window class with drag methods of its own, as SDL's is, and one made from it that calls up to them
static int ownEntered = 0, ownPerformed = 0, subclassEntered = 0;
@interface NativeDesktopTestWindow : NSWindow
@end
@implementation NativeDesktopTestWindow
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { ownEntered++; return NSDragOperationLink; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender { ownPerformed++; return YES; }
@end
@interface NativeDesktopTestSubwindow : NativeDesktopTestWindow
@end
@implementation NativeDesktopTestSubwindow
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { subclassEntered++; return [super draggingEntered:sender]; }
@end
// What AppKit gives the methods, for a drag at a point in the window from a source that allows copy and link
@interface NativeDesktopTestDrag : NSObject
@end
@implementation NativeDesktopTestDrag
- (NSPoint)draggingLocation { return NSMakePoint (10, 44); }
- (NSDragOperation)draggingSourceOperationMask { return NSDragOperationCopy | NSDragOperationLink; }
- (NSPasteboard*)draggingPasteboard { return nil; }
@end
// Watches whether the window is visible, as SDL does with its windows
@interface NativeDesktopTestObserver : NSObject
@end
@implementation NativeDesktopTestObserver
- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary*)change context:(void*)context {}
@end

// Inside the namespace, its names are found before any of AppKit's
namespace native_desktop {

static int heard = 0;
static DragEvent last;
static int Answer (const DragEvent& event, DragData*, void*) { heard++; last = event; return DROP_ACTION_COPY; }
static NSWindow* CreateWindow (Class windowClass) {
 NSWindow* window = [[windowClass alloc] initWithContentRect:NSMakeRect (100, 100, 64, 64) styleMask:NSWindowStyleMaskTitled
  backing:NSBackingStoreBuffered defer:NO];
 [window setReleasedWhenClosed:NO];
 return window;
}
static Window Borrow (NSWindow* window, uint32_t id) {
 Window borrowed;
 borrowed.handle = (__bridge void*)window;
 borrowed.id = id;
 return borrowed;
}
static id<NSDraggingDestination> Destination (NSWindow* window) { return (id<NSDraggingDestination>)window; }
// Removes the observer as SDL does when it closes the window, giving whether that was allowed
static bool RemoveObserver (NSWindow* window, NSObject* observer) {
 @try {
  [window removeObserver:observer forKeyPath:@"visible"];
  return true;
 } @catch (NSException* exception) {
  std::cerr << [[exception name] UTF8String] << ": " << [[exception reason] UTF8String] << "\n";
  return false;
 }
}
static int Run () {
 [NSApplication sharedApplication];
 [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
 [NSApp finishLaunching];
 DragDrop::SetHandler (Answer, NULL);
 id<NSDraggingInfo> drag = (id<NSDraggingInfo>)[[NativeDesktopTestDrag alloc] init];
 NativeDesktopTestObserver* observer = [[NativeDesktopTestObserver alloc] init];
 SEL selectors[] = { @selector (draggingEntered:), @selector (draggingUpdated:), @selector (draggingExited:),
  @selector (prepareForDragOperation:), @selector (performDragOperation:), @selector (concludeDragOperation:) };
 const int count = sizeof (selectors) / sizeof (selectors[0]);

 // A window that was a target keeps its class, and the host's observers of it can be removed, as Key-Value
 // Observing keeps them by class
 NSWindow* first = CreateWindow ([NativeDesktopTestWindow class]);
 NSWindow* second = CreateWindow ([NativeDesktopTestWindow class]);
 bool responded[count];
 for (int i = 0; i < count; i++) responded[i] = [second respondsToSelector:selectors[i]];
 [first addObserver:observer forKeyPath:@"visible" options:NSKeyValueObservingOptionNew context:NULL];
 Class firstClass = object_getClass (first);
 Window a = Borrow (first, 7);
 assert (DragDrop::SetTarget (&a, true));
 assert (DragDrop::SetTarget (&a, false));
 assert (RemoveObserver (first, observer));
 assert (object_getClass (first) == firstClass);

 // So can those of a window that is still a target, as when the host quits
 [first addObserver:observer forKeyPath:@"visible" options:NSKeyValueObservingOptionNew context:NULL];
 assert (DragDrop::SetTarget (&a, true));
 assert (RemoveObserver (first, observer));

 // A target's drags go to the handler, at the point in its content view from the top left
 assert ([Destination (first) draggingEntered:drag] == NSDragOperationCopy && heard == 1);
 assert (last.type == DRAG_ENTER && last.window == 7 && last.x == 10 && last.y == 20 && last.allowedActions == (DROP_ACTION_COPY | DROP_ACTION_LINK));
 assert ([Destination (first) draggingUpdated:drag] == NSDragOperationCopy && heard == 2 && last.type == DRAG_OVER);
 [Destination (first) draggingExited:drag];
 assert (heard == 3 && last.type == DRAG_LEAVE);
 assert ([Destination (first) prepareForDragOperation:drag]);
 assert ([Destination (first) performDragOperation:drag] && heard == 4 && last.type == DRAG_DROP);
 [Destination (first) concludeDragOperation:drag];
 assert (heard == 4 && ownEntered == 0 && ownPerformed == 0);

 // Another window of its class keeps its own drags, and answers for the same methods as it did
 assert ([Destination (second) draggingEntered:drag] == NSDragOperationLink && ownEntered == 1);
 assert ([Destination (second) performDragOperation:drag] && ownPerformed == 1 && heard == 4);
 for (int i = 0; i < count; i++) assert ([second respondsToSelector:selectors[i]] == responded[i]);

 // Given back, a window has its own drags again
 assert (DragDrop::SetTarget (&a, false));
 assert ([Destination (first) draggingEntered:drag] == NSDragOperationLink && ownEntered == 2 && heard == 4);

 // A window whose class calls up to its own class's methods takes its drags as a target, and calls up once
 // when it is not, with windows of both classes targets
 NSWindow* third = CreateWindow ([NativeDesktopTestSubwindow class]);
 NSWindow* fourth = CreateWindow ([NativeDesktopTestSubwindow class]);
 Window c = Borrow (third, 8);
 assert (DragDrop::SetTarget (&a, true));
 assert (DragDrop::SetTarget (&c, true));
 assert ([Destination (third) draggingEntered:drag] == NSDragOperationCopy && heard == 5 && last.window == 8 && subclassEntered == 0);
 assert ([Destination (fourth) draggingEntered:drag] == NSDragOperationLink && subclassEntered == 1 && ownEntered == 3 && heard == 5);
 assert (DragDrop::SetTarget (&a, false));
 assert (DragDrop::SetTarget (&c, false));

 [first close];
 [second close];
 [third close];
 [fourth close];
 DragDrop::SetHandler (NULL, NULL);
 std::cout << "macOS drop targets, which keep their window's class, passed\n";
 return 0;
}
}
int main () {
 @autoreleasepool {
  return native_desktop::Run ();
 }
}
