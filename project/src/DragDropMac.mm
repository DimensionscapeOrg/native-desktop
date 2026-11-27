#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_MAC

// Drag and drop on macOS. The dragging destination methods of the host's window class, which already takes
// file drags, are replaced by ones that come here for the windows that are targets and go to the class's own
// for the others. The window keeps its class: Key-Value Observing, which SDL uses on its windows, keeps
// observers by class, and those of a window given another class can't be removed. Drags out begin a dragging
// session from the window's content view.

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "ClipboardMacFormats.h"
#include <map>
#include <set>

#if __has_feature(objc_arc)
#define NATIVE_DESKTOP_RETAIN(object) (object)
#define NATIVE_DESKTOP_RELEASE(object)
#else
#define NATIVE_DESKTOP_RETAIN(object) [(object) retain]
#define NATIVE_DESKTOP_RELEASE(object) [(object) release]
#endif


namespace native_desktop {


	static std::map<void*, uint32_t> targetWindows;
	static std::set<void*> targetClasses;


	class PasteboardDragData : public DragData {

		public:

			PasteboardDragData (NSPasteboard* pasteboard) : pasteboard (pasteboard) {}

			bool HasFormat (int format) { return MacPasteboardHasFormat (pasteboard, format); }
			bool GetText (int format, std::string* text) { return MacPasteboardGetText (pasteboard, format, text); }
			bool GetFiles (std::vector<std::string>* files) { return MacPasteboardGetFiles (pasteboard, files); }
			bool GetImage (std::vector<unsigned char>* pixels, int* width, int* height) { return MacPasteboardGetImage (pasteboard, pixels, width, height); }

		private:

			NSPasteboard* pasteboard;

	};


	static NSArray* GetDragTypes () {

		return @[ NSPasteboardTypeFileURL, NSPasteboardTypeString, NSPasteboardTypeHTML, NSPasteboardTypeRTF, NSPasteboardTypeURL, NSPasteboardTypeTIFF, NSPasteboardTypePNG ];

	}


	static int GetActions (NSDragOperation operations) {

		int actions = DROP_ACTION_NONE;
		if (operations & NSDragOperationCopy) actions |= DROP_ACTION_COPY;
		if (operations & (NSDragOperationMove | NSDragOperationGeneric)) actions |= DROP_ACTION_MOVE;
		if (operations & NSDragOperationLink) actions |= DROP_ACTION_LINK;
		return actions;

	}


	static NSDragOperation GetOperations (int actions) {

		NSDragOperation operations = NSDragOperationNone;
		if (actions & DROP_ACTION_COPY) operations |= NSDragOperationCopy;
		if (actions & DROP_ACTION_MOVE) operations |= NSDragOperationMove | NSDragOperationGeneric;
		if (actions & DROP_ACTION_LINK) operations |= NSDragOperationLink;
		return operations;

	}


	static int GetAction (NSDragOperation operation) {

		if (operation & NSDragOperationCopy) return DROP_ACTION_COPY;
		if (operation & (NSDragOperationMove | NSDragOperationGeneric)) return DROP_ACTION_MOVE;
		if (operation & NSDragOperationLink) return DROP_ACTION_LINK;
		return DROP_ACTION_NONE;

	}


	static NSDragOperation SendDrag (NSWindow* window, int type, id<NSDraggingInfo> info) {

		std::map<void*, uint32_t>::iterator found = targetWindows.find ((__bridge void*)window);
		if (found == targetWindows.end ()) return NSDragOperationNone;

		// Window events use the content view's points, from its top left
		NSView* view = [window contentView];
		NSPoint point = [view convertPoint:[info draggingLocation] fromView:nil];
		int y = [view isFlipped] ? (int)point.y : (int)([view bounds].size.height - point.y);

		DragEvent event = { type, found->second, (int)point.x, y, GetActions ([info draggingSourceOperationMask]) };
		PasteboardDragData data ([info draggingPasteboard]);
		int action = DragDrop::Dispatch (event, type == DRAG_LEAVE ? NULL : &data);

		if (action & DROP_ACTION_COPY) return NSDragOperationCopy;
		if (action & DROP_ACTION_MOVE) return ([info draggingSourceOperationMask] & NSDragOperationMove) ? NSDragOperationMove : NSDragOperationGeneric;
		if (action & DROP_ACTION_LINK) return NSDragOperationLink;
		return NSDragOperationNone;

	}


	static bool IsTarget (id window) {

		return targetWindows.find ((__bridge void*)window) != targetWindows.end ();

	}


	static NSDragOperation Entered (NSWindow* window, id<NSDraggingInfo> info) { return SendDrag (window, DRAG_ENTER, info); }
	static NSDragOperation Updated (NSWindow* window, id<NSDraggingInfo> info) { return SendDrag (window, DRAG_OVER, info); }
	static void Exited (NSWindow* window, id<NSDraggingInfo> info) { SendDrag (window, DRAG_LEAVE, info); }
	static BOOL Prepare (NSWindow* window, id<NSDraggingInfo> info) { return YES; }
	static BOOL Perform (NSWindow* window, id<NSDraggingInfo> info) { return SendDrag (window, DRAG_DROP, info) != NSDragOperationNone; }
	static void Conclude (NSWindow* window, id<NSDraggingInfo> info) {}


	// The class's own implementation, or null when it inherits one
	static IMP GetOwnImplementation (Class windowClass, SEL selector) {

		Method method = class_getInstanceMethod (windowClass, selector);
		Class superclass = class_getSuperclass (windowClass);
		if (!method || (superclass && class_getInstanceMethod (superclass, selector) == method)) return NULL;
		return method_getImplementation (method);

	}


	// What the class does for windows that aren't targets: its own implementation, or the one it inherits,
	// looked up as it is called, as a call to super is
	static IMP GetOriginal (Class windowClass, SEL selector, IMP own) {

		if (own) return own;
		Class superclass = class_getSuperclass (windowClass);
		return superclass && class_respondsToSelector (superclass, selector) ? class_getMethodImplementation (superclass, selector) : NULL;

	}


	static void SetImplementation (Class windowClass, SEL selector, IMP own, id block) {

		IMP implementation = imp_implementationWithBlock (block);

		if (own) {

			method_setImplementation (class_getInstanceMethod (windowClass, selector), implementation);

		} else {

			struct objc_method_description description = protocol_getMethodDescription (@protocol (NSDraggingDestination), selector, NO, YES);
			class_addMethod (windowClass, selector, implementation, description.types);

		}

	}


	// Each replacement knows the class it was made for, so a class that calls up to its own class's method,
	// itself replaced, goes on from there
	template<typename Result> static void Replace (Class windowClass, SEL selector, Result (*target) (NSWindow*, id<NSDraggingInfo>), Result fallback) {

		IMP own = GetOwnImplementation (windowClass, selector);

		SetImplementation (windowClass, selector, own, ^Result (id window, id<NSDraggingInfo> info) {

			if (IsTarget (window)) return target ((NSWindow*)window, info);
			IMP original = GetOriginal (windowClass, selector, own);
			return original ? ((Result (*) (id, SEL, id))original) (window, selector, info) : fallback;

		});

	}


	static void Replace (Class windowClass, SEL selector, void (*target) (NSWindow*, id<NSDraggingInfo>)) {

		IMP own = GetOwnImplementation (windowClass, selector);

		SetImplementation (windowClass, selector, own, ^(id window, id<NSDraggingInfo> info) {

			if (IsTarget (window)) {

				target ((NSWindow*)window, info);
				return;

			}

			IMP original = GetOriginal (windowClass, selector, own);
			if (original) ((void (*) (id, SEL, id))original) (window, selector, info);

		});

	}


	// The class the window was made with, under any that Key-Value Observing made for it
	static Class GetWindowClass (NSWindow* window) {

		Class windowClass = [window class];

		for (Class current = object_getClass (window); current; current = class_getSuperclass (current)) {

			if (current == windowClass) return windowClass;

		}

		return object_getClass (window);

	}


	// Once for each window class, whose windows that aren't targets go on as before
	static void AddTargetMethods (Class windowClass) {

		if (!targetClasses.insert ((__bridge void*)windowClass).second) return;

		Replace<NSDragOperation> (windowClass, @selector (draggingEntered:), Entered, NSDragOperationNone);
		Replace<NSDragOperation> (windowClass, @selector (draggingUpdated:), Updated, NSDragOperationNone);
		Replace (windowClass, @selector (draggingExited:), Exited);
		Replace<BOOL> (windowClass, @selector (prepareForDragOperation:), Prepare, YES);
		Replace<BOOL> (windowClass, @selector (performDragOperation:), Perform, NO);
		Replace (windowClass, @selector (concludeDragOperation:), Conclude);

	}


	static NSImage* CreateDragImage (Image* image) {

		NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:image->width pixelsHigh:image->height
			bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
			bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:image->width * 4 bitsPerPixel:32];

		if (!rep) return nil;

		for (int y = 0; y < image->height; y++) {

			memcpy ([rep bitmapData] + (size_t)y * image->width * 4, image->data + (size_t)y * image->stride, (size_t)image->width * 4);

		}

		NSImage* result = [[NSImage alloc] initWithSize:NSMakeSize (image->width, image->height)];
		[result addRepresentation:rep];
		NATIVE_DESKTOP_RELEASE (rep);

		#if __has_feature(objc_arc)
		return result;
		#else
		return [result autorelease];
		#endif

	}


}


using namespace native_desktop;


@interface NativeDesktopDragSource : NSObject <NSDraggingSource> {

	@public int actions;
	@public int dragID;

}

@end


@implementation NativeDesktopDragSource

- (NSDragOperation)draggingSession:(NSDraggingSession*)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context {

	return GetOperations (actions);

}

- (void)draggingSession:(NSDraggingSession*)session endedAtPoint:(NSPoint)point operation:(NSDragOperation)operation {

	DragDrop::PushComplete (dragID, GetAction (operation));

	// The source kept itself alive for the session
	#if __has_feature(objc_arc)
	CFBridgingRelease ((__bridge CFTypeRef)self);
	#else
	[self release];
	#endif

}

@end


namespace native_desktop {


	bool DragDrop::IsSupported () {

		return true;

	}


	bool DragDrop::SetTarget (Window* window, bool enabled) {

		if (!window || !window->handle || !Menu::IsOwnerThread ()) return false;

		@autoreleasepool {

			NSWindow* nsWindow = (__bridge NSWindow*)window->handle;
			if (![nsWindow isKindOfClass:[NSWindow class]]) return false;

			if (enabled) {

				AddTargetMethods (GetWindowClass (nsWindow));
				targetWindows[window->handle] = window->id;
				[nsWindow registerForDraggedTypes:GetDragTypes ()];

			} else {

				targetWindows.erase (window->handle);

				// The window takes file drags again, as its own class asked
				[nsWindow unregisterDraggedTypes];
				[nsWindow registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];

			}

			return true;

		}

	}


	int DragDrop::StartDrag (Window* window, const std::vector<ClipboardItem>& items, Image* image, int offsetX, int offsetY, int allowedActions, int id) {

		if (!window || !window->handle || !Menu::IsOwnerThread ()) return DROP_ACTION_NONE;

		@autoreleasepool {

			NSWindow* nsWindow = (__bridge NSWindow*)window->handle;
			NSView* view = [nsWindow contentView];
			NSEvent* event = [NSApp currentEvent];

			// A dragging session starts from the mouse event that began it
			if (!view || !event || ([event type] != NSEventTypeLeftMouseDown && [event type] != NSEventTypeLeftMouseDragged)) return DROP_ACTION_NONE;

			NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
			bool itemHasData = false;
			NSMutableArray* writers = [NSMutableArray array];

			for (size_t i = 0; i < items.size (); i++) {

				const ClipboardItem& clip = items[i];
				NSString* string = [NSString stringWithUTF8String:clip.text.c_str ()];

				switch (clip.format) {

					case CLIPBOARD_TEXT:
						itemHasData = [item setString:(string ? string : @"") forType:NSPasteboardTypeString] || itemHasData;
						break;

					case CLIPBOARD_HTML:
						itemHasData = [item setString:(string ? string : @"") forType:NSPasteboardTypeHTML] || itemHasData;
						break;

					case CLIPBOARD_RICH_TEXT:
						itemHasData = [item setData:[NSData dataWithBytes:clip.text.data () length:clip.text.size ()] forType:NSPasteboardTypeRTF] || itemHasData;
						break;

					case CLIPBOARD_URL: {

						NSURL* url = string ? [NSURL URLWithString:string] : nil;
						if (url) [writers addObject:url];
						break;

					}

					case CLIPBOARD_FILE_LIST:

						for (size_t j = 0; j < clip.files.size (); j++) {

							NSString* path = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:clip.files[j].c_str () length:clip.files[j].size ()];
							if (path) [writers addObject:[NSURL fileURLWithPath:path]];

						}
						break;

					case CLIPBOARD_IMAGE: {

						if (clip.width <= 0 || clip.height <= 0 || clip.pixels.size () < (size_t)clip.width * clip.height * 4) break;

						Image pixels;
						pixels.data = &clip.pixels[0];
						pixels.length = clip.pixels.size ();
						pixels.width = clip.width;
						pixels.height = clip.height;
						pixels.stride = clip.width * 4;

						NSImage* picture = CreateDragImage (&pixels);
						NSBitmapImageRep* rep = picture ? (NSBitmapImageRep*)[[picture representations] firstObject] : nil;
						NSData* png = rep ? [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] : nil;
						if (png) itemHasData = [item setData:png forType:NSPasteboardTypePNG] || itemHasData;
						if (rep) itemHasData = [item setData:[rep TIFFRepresentation] forType:NSPasteboardTypeTIFF] || itemHasData;
						break;

					}

					default: break;

				}

			}

			if (itemHasData) [writers insertObject:item atIndex:0];
			NATIVE_DESKTOP_RELEASE (item);
			if ([writers count] == 0) return DROP_ACTION_NONE;

			// The image sits under the pointer, offset as asked, and the other items go with it unseen
			NSPoint point = [view convertPoint:[event locationInWindow] fromView:nil];
			NSImage* picture = image && image->Valid () ? CreateDragImage (image) : nil;
			NSSize size = picture ? [picture size] : NSMakeSize (1, 1);
			CGFloat top = [view isFlipped] ? point.y - offsetY : point.y + offsetY - size.height;
			NSRect frame = NSMakeRect (point.x - offsetX, top, size.width, size.height);

			NSMutableArray* draggingItems = [NSMutableArray array];

			for (NSUInteger i = 0; i < [writers count]; i++) {

				NSDraggingItem* draggingItem = [[NSDraggingItem alloc] initWithPasteboardWriter:[writers objectAtIndex:i]];
				[draggingItem setDraggingFrame:frame contents:(i == 0 ? picture : nil)];
				[draggingItems addObject:draggingItem];
				NATIVE_DESKTOP_RELEASE (draggingItem);

			}

			NativeDesktopDragSource* source = [[NativeDesktopDragSource alloc] init];
			source->actions = allowedActions ? allowedActions : DROP_ACTION_COPY;
			source->dragID = id;

			// The source lets itself go when the session ends
			#if __has_feature(objc_arc)
			CFBridgingRetain (source);
			#endif

			// Without ARC, the alloc's reference is the one endedAtPoint lets go
			[view beginDraggingSessionWithItems:draggingItems event:event source:source];
			return -1;

		}

	}


	void DragDrop::Update () {}


}


#endif
