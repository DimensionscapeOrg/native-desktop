#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_MAC

// Stacking, moving and resizing windows on macOS, which SDL has no calls for

#import <Cocoa/Cocoa.h>


namespace native_desktop {


	static NSWindow* GetControlledWindow (Window* window) {

		return window ? (__bridge NSWindow*)window->handle : nil;

	}


	// AppKit puts a window on screen to order it, so a hidden or minimised window would be shown, while its host
	// still takes it for hidden. Neither is ordered, nor is a window ordered against one that isn't on screen.
	static bool IsOnScreen (NSWindow* window) {

		return window && [window isVisible] && ![window isMiniaturized];

	}


	// Follows the pointer until the left button is released, as AppKit's own loops do. performWindowDragWithEvent:
	// would need the mouse down event itself, which the host has already handled, and there is no call for resizing.
	static bool TrackDrag (NSWindow* window, int edges) {

		if (!window || !([NSEvent pressedMouseButtons] & 1)) return false;

		NSPoint start = [NSEvent mouseLocation];
		NSRect frame = [window frame];
		NSSize minSize = [window minSize];
		NSSize maxSize = [window maxSize];

		while (true) {

			@autoreleasepool {

				NSEvent* event = [NSApp nextEventMatchingMask:(NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp) untilDate:[NSDate distantFuture]
					inMode:NSEventTrackingRunLoopMode dequeue:YES];

				if (!event || [event type] == NSEventTypeLeftMouseUp) {

					// The host hears of the release, as it would have without the loop
					if (event) [NSApp sendEvent:event];
					break;

				}

				NSPoint point = [NSEvent mouseLocation];
				CGFloat dx = point.x - start.x;
				CGFloat dy = point.y - start.y;
				NSRect next = frame;

				if (edges == 0) {

					next.origin.x += dx;
					next.origin.y += dy;

				} else {

					// AppKit's origin is the bottom left, so the top edge moves the height and the bottom both
					if (edges & WINDOW_EDGE_RIGHT) next.size.width = frame.size.width + dx;
					if (edges & WINDOW_EDGE_LEFT) next.size.width = frame.size.width - dx;
					if (edges & WINDOW_EDGE_TOP) next.size.height = frame.size.height + dy;
					if (edges & WINDOW_EDGE_BOTTOM) next.size.height = frame.size.height - dy;

					next.size.width = MAX (minSize.width, MIN (maxSize.width, next.size.width));
					next.size.height = MAX (minSize.height, MIN (maxSize.height, next.size.height));

					if (edges & WINDOW_EDGE_LEFT) next.origin.x = frame.origin.x + frame.size.width - next.size.width;
					if (edges & WINDOW_EDGE_BOTTOM) next.origin.y = frame.origin.y + frame.size.height - next.size.height;

				}

				[window setFrame:next display:YES];

			}

		}

		return true;

	}


	bool WindowControl::OrderToBack (Window* window) {

		NSWindow* nsWindow = GetControlledWindow (window);
		if (!IsOnScreen (nsWindow)) return false;

		[nsWindow orderBack:nil];
		return true;

	}


	bool WindowControl::OrderAbove (Window* window, Window* other) {

		NSWindow* nsWindow = GetControlledWindow (window);
		NSWindow* otherWindow = GetControlledWindow (other);
		if (!IsOnScreen (nsWindow) || !IsOnScreen (otherWindow) || nsWindow == otherWindow) return false;

		[nsWindow orderWindow:NSWindowAbove relativeTo:[otherWindow windowNumber]];
		return true;

	}


	bool WindowControl::OrderBelow (Window* window, Window* other) {

		NSWindow* nsWindow = GetControlledWindow (window);
		NSWindow* otherWindow = GetControlledWindow (other);
		if (!IsOnScreen (nsWindow) || !IsOnScreen (otherWindow) || nsWindow == otherWindow) return false;

		[nsWindow orderWindow:NSWindowBelow relativeTo:[otherWindow windowNumber]];
		return true;

	}


	bool WindowControl::StartMove (Window* window) {

		return TrackDrag (GetControlledWindow (window), 0);

	}


	bool WindowControl::StartResize (Window* window, int edges) {

		edges &= WINDOW_EDGE_TOP | WINDOW_EDGE_BOTTOM | WINDOW_EDGE_LEFT | WINDOW_EDGE_RIGHT;
		return edges != 0 && TrackDrag (GetControlledWindow (window), edges);

	}


}


#endif
